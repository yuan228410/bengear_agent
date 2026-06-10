#include "ben_gear/memory/context_pruner.hpp"

#include <algorithm>
#include <sstream>

namespace ben_gear::memory {

// CJK 感知 token 估算（文件作用域前向声明）
static int64_t estimate_text_tokens(std::string_view text);

// ====================================================================
// prune
// ====================================================================

container::Vector<acp::ACPMessage> ContextPruner::prune(
  const container::Vector<acp::ACPMessage>& history,
  const Options& opts) {

 if (history.empty()) {
  log::debug_fmt("context_pruner: empty history, skip");
  return history;
 }

 log::info_fmt("context_pruner: start, history={} msgs, protect_recent={}, soft_lines={}, hard_after={}, max_chars={}",
               history.size(), opts.protect_recent, opts.soft_prune_lines,
               opts.hard_prune_after, opts.max_tool_result_chars);

 // 1. 统计助手消息轮次，从最新往回编号
 int assistant_count = 0;
 container::Vector<int> msg_assistant_index(history.size(), -1);
 for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
  if (history[i].role() == acp::Role::Assistant) {
   assistant_count++;
   msg_assistant_index[i] = assistant_count;
  }
 }

 log::debug_fmt("context_pruner: assistant_count={}", assistant_count);

 // 2. 遍历消息，按轮次深度裁剪工具结果
 container::Vector<acp::ACPMessage> result;
 result.reserve(history.size());

 int hard_pruned = 0;
 int soft_pruned = 0;

 for (int i = 0; i < static_cast<int>(history.size()); ++i) {
  const auto& msg = history[i];

  if (msg.role() == acp::Role::Tool) {
   // 找到前面最近的助手消息的 depth
   int nearest_depth = -1;
   for (int j = i - 1; j >= 0; --j) {
    if (history[j].role() == acp::Role::Assistant) {
     nearest_depth = msg_assistant_index[j];
     break;
    }
   }

   if (nearest_depth > 0 && nearest_depth <= opts.protect_recent) {
    // 保护区内：完整保留
    log::debug_fmt("context_pruner: msg[{}] tool result protected, depth={}", i, nearest_depth);
    result.push_back(msg);
    continue;
   }

   // 裁剪工具结果内容
   acp::ACPMessage pruned_msg;
   pruned_msg.set_role(msg.role());

   for (const auto& block : msg.content()) {
    if (block.is_tool_result()) {
     const auto& tr = block.tool_result();
     auto output_len = tr.output.size();

     if (nearest_depth > 0 && nearest_depth > opts.hard_prune_after) {
      // 硬裁剪：替换为占位符
      hard_pruned++;
      log::info_fmt("context_pruner: msg[{}] hard prune, depth={}, tool_id={}, orig_len={}",
                    i, nearest_depth,
                    std::string_view(tr.tool_call_id.data(), tr.tool_call_id.size()),
                    output_len);
      llm::ToolCallResult placeholder;
      placeholder.tool_call_id = tr.tool_call_id;
      placeholder.output = container::String("[tool result pruned]");
      placeholder.success = tr.success;
      pruned_msg.add_tool_result(std::move(placeholder));
     } else if (output_len > static_cast<size_t>(opts.max_tool_result_chars)) {
      // 软裁剪：首尾几行 + 省略号
      soft_pruned++;
      log::info_fmt("context_pruner: msg[{}] soft prune, depth={}, tool_id={}, orig_len={}",
                    i, nearest_depth,
                    std::string_view(tr.tool_call_id.data(), tr.tool_call_id.size()),
                    output_len);
      llm::ToolCallResult pruned_tr;
      pruned_tr.tool_call_id = tr.tool_call_id;
      pruned_tr.output = soft_prune(tr.output, opts.soft_prune_lines);
      pruned_tr.success = tr.success;
      pruned_msg.add_tool_result(std::move(pruned_tr));
     } else {
      pruned_msg.add_content(block);
     }
    } else {
     pruned_msg.add_content(block);
    }
   }

   result.push_back(std::move(pruned_msg));
  } else if (msg.role() == acp::Role::Assistant) {
   int depth = msg_assistant_index[i];

   // 助手消息：裁剪其中的内联工具调用结果
   if (depth > 0 && depth <= opts.protect_recent) {
    result.push_back(msg);
    continue;
   }

   acp::ACPMessage pruned_msg;
   pruned_msg.set_role(msg.role());

   for (const auto& block : msg.content()) {
    if (block.is_tool_result()) {
     const auto& tr = block.tool_result();

     if (depth > opts.hard_prune_after) {
      hard_pruned++;
      log::info_fmt("context_pruner: msg[{}] assistant inline hard prune, depth={}, tool_id={}",
                    i, depth,
                    std::string_view(tr.tool_call_id.data(), tr.tool_call_id.size()));
      llm::ToolCallResult placeholder;
      placeholder.tool_call_id = tr.tool_call_id;
      placeholder.output = container::String("[tool result pruned]");
      placeholder.success = tr.success;
      pruned_msg.add_tool_result(std::move(placeholder));
     } else if (tr.output.size() > static_cast<size_t>(opts.max_tool_result_chars)) {
      soft_pruned++;
      log::info_fmt("context_pruner: msg[{}] assistant inline soft prune, depth={}, tool_id={}",
                    i, depth,
                    std::string_view(tr.tool_call_id.data(), tr.tool_call_id.size()));
      llm::ToolCallResult pruned_tr;
      pruned_tr.tool_call_id = tr.tool_call_id;
      pruned_tr.output = soft_prune(tr.output, opts.soft_prune_lines);
      pruned_tr.success = tr.success;
      pruned_msg.add_tool_result(std::move(pruned_tr));
     } else {
      pruned_msg.add_content(block);
     }
    } else {
     pruned_msg.add_content(block);
    }
   }

   result.push_back(std::move(pruned_msg));
  } else {
   // system / user 消息：不裁剪
   result.push_back(msg);
  }
 }

 auto before_tokens = estimate_tokens(history);
 auto after_tokens = estimate_tokens(result);
 log::info_fmt("context_pruner: done, {} msgs, hard={}, soft={}, tokens {} → {} (saved {})",
               result.size(), hard_pruned, soft_pruned,
               before_tokens, after_tokens, before_tokens - after_tokens);

 return result;
}

// ====================================================================
// estimate_tokens
// ====================================================================

int64_t ContextPruner::estimate_tokens(const acp::ACPMessage& msg) {
 int64_t count = 0;
 for (const auto& block : msg.content()) {
  if (block.is_text()) {
   const auto& t = block.text();
   count += estimate_text_tokens(std::string_view(t.data(), t.size()));
  } else if (block.is_tool_result()) {
   const auto& tr = block.tool_result();
   count += estimate_text_tokens(std::string_view(tr.output.data(), tr.output.size()));
  } else if (block.is_tool_use()) {
   const auto& tu = block.tool_use();
   count += estimate_text_tokens(std::string_view(tu.name.data(), tu.name.size()));
   count += estimate_text_tokens(tu.arguments.dump());
  }
 }
 return count;
}

int64_t ContextPruner::estimate_tokens(const container::Vector<acp::ACPMessage>& msgs) {
 int64_t total = 0;
 for (const auto& msg : msgs) {
  total += estimate_tokens(msg);
 }
 return total;
}

// ====================================================================
// soft_prune
// ====================================================================

container::String ContextPruner::soft_prune(const container::String& content, int keep_lines) {
 std::string_view sv(content.data(), content.size());

 // 按行拆分
 container::Vector<std::string_view> lines;
 size_t start = 0;
 for (size_t i = 0; i < sv.size(); ++i) {
  if (sv[i] == '\n') {
   lines.push_back(sv.substr(start, i - start));
   start = i + 1;
  }
 }
 if (start < sv.size()) {
  lines.push_back(sv.substr(start));
 }

 // 行数足够多时：按行裁剪
 if (static_cast<int>(lines.size()) > keep_lines * 2) {
  std::string result;
  result.reserve(keep_lines * 80 * 2 + 50);

  for (int i = 0; i < keep_lines && i < static_cast<int>(lines.size()); ++i) {
   if (i > 0) result += '\n';
   result += lines[i];
  }

  result += "\n... (";
  result += std::to_string(lines.size() - keep_lines * 2);
  result += " lines omitted) ...\n";

  int start_end = static_cast<int>(lines.size()) - keep_lines;
  for (int i = start_end; i < static_cast<int>(lines.size()); ++i) {
   if (i > start_end) result += '\n';
   result += lines[i];
  }

  log::debug_fmt("context_pruner: soft_prune by lines, {} lines → {}+{}", lines.size(), keep_lines, keep_lines);
  return container::String(result);
 }

 // 行数不多但内容很长：按字符截断
 if (content.size() > static_cast<size_t>(keep_lines * 160)) {
  size_t keep_chars = static_cast<size_t>(keep_lines) * 80;
  std::string result(sv.substr(0, keep_chars));
  result += "\n... (";
  result += std::to_string(content.size() - keep_chars * 2);
  result += " chars omitted) ...\n";
  result += sv.substr(sv.size() - keep_chars);

  log::debug_fmt("context_pruner: soft_prune by chars, {} chars → {}+{}", content.size(), keep_chars, keep_chars);
  return container::String(result);
 }

 // 内容短：不裁剪
 return content;
}

// ====================================================================
// is_stale_tool_error
// ====================================================================

bool ContextPruner::is_stale_tool_error(const container::String& content) {
 auto sv = std::string_view(content.data(), content.size());
 return sv.find("permission denied") != std::string_view::npos ||
        sv.find("not allowed") != std::string_view::npos ||
        sv.find("execution denied") != std::string_view::npos;
}

// ====================================================================
// estimate_text_tokens — CJK 感知
// ====================================================================

static int64_t estimate_text_tokens(std::string_view text) {
 int64_t cjk = 0;
 int64_t ascii = 0;
 for (size_t i = 0; i < text.size(); ) {
  unsigned char c = static_cast<unsigned char>(text[i]);
  if (c >= 0xF0)      { cjk++; i += 4; }
  else if (c >= 0xE0) { cjk++; i += 3; }
  else if (c >= 0xC0) { cjk++; i += 2; }
  else                { ascii++; i += 1; }
 }
 // CJK 1 字符 ≈ 1 token，ASCII 约 4 字符 ≈ 1 token
 return cjk + std::max<int64_t>(1, ascii / 4);
}

} // namespace ben_gear::memory
