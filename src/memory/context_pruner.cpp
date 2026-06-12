#include "ben_gear/memory/context_pruner.hpp"

#include <algorithm>
#include <sstream>

namespace ben_gear::memory {

// CJK 感知 token 估算（文件作用域前向声明）
static int64_t estimate_text_tokens(std::string_view text);

// ====================================================================
// compute_depths
// ====================================================================

container::Vector<int> ContextPruner::compute_depths(
  const container::Vector<acp::ACPMessage>& history) {
 container::Vector<int> depths;
 depths.reserve(history.size());
 int assistant_count = 0;
 // 从最新往回编号
 // 先计算所有 depth，再反转存储
 for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
  if (history[i].role() == acp::Role::Assistant) {
   assistant_count++;
  }
 }
 // 正向填充：每条消息的 depth
 int current = assistant_count;
 for (size_t i = 0; i < history.size(); ++i) {
  if (history[i].role() == acp::Role::Assistant) {
   depths.push_back(current);
   current--;
  } else {
   depths.push_back(-1);
  }
 }
 return depths;
}

// ====================================================================
// prune — 全量裁剪
// ====================================================================

ContextPruner::PruneResult ContextPruner::prune(
  const container::Vector<acp::ACPMessage>& history,
  const Options& opts) {

 if (history.empty()) {
  log::debug_fmt("context_pruner: empty history, skip");
  return PruneResult{history, 0, 0, 0, 0};
 }

 log::info_fmt("context_pruner: start, history={} msgs, protect_recent={}, soft_lines={}, hard_after={}, max_chars={}",
               history.size(), opts.protect_recent, opts.soft_prune_lines,
               opts.hard_prune_after, opts.max_tool_result_chars);

 auto depths = compute_depths(history);
 auto result = prune_range_with_depths(history, 0, depths, opts);

 log::info_fmt("context_pruner: done, {} msgs, hard={}, soft={}, stripped_msgs={}, stripped_uses={}",
               result.messages.size(), result.hard_pruned, result.soft_pruned,
               result.stripped_msgs, result.stripped_uses);

 return result;
}

// ====================================================================
// prune_range_with_depths — 增量裁剪核心
// ====================================================================

ContextPruner::PruneResult ContextPruner::prune_range_with_depths(
  const container::Vector<acp::ACPMessage>& history,
  size_t start,
  const container::Vector<int>& depths,
  const Options& opts) {

container::Vector<acp::ACPMessage> result;
result.reserve(history.size() - start);

int hard_pruned = 0;
int soft_pruned = 0;
int stripped_msgs = 0;
int stripped_uses = 0;

for (size_t idx = start; idx < history.size(); ++idx) {
 const auto& msg = history[idx];

 if (msg.role() == acp::Role::Tool) {
  // 找到前面最近的助手消息的 depth
  int nearest_depth = -1;
  for (int j = static_cast<int>(idx) - 1; j >= 0; --j) {
   if (history[j].role() == acp::Role::Assistant) {
    nearest_depth = depths[j];
    break;
   }
  }

  if (nearest_depth > 0 && nearest_depth <= opts.protect_recent) {
   // 保护区内：完整保留
   result.push_back(msg);
   continue;
  }

  // 剥离区：整条删除 tool result 消息
  if (nearest_depth > 0 && nearest_depth > opts.hard_prune_after) {
   stripped_msgs++;
   log::debug_fmt("context_pruner: msg[{}] stripped tool result, depth={}",
                 idx, nearest_depth);
   continue;
  }

  // 软裁剪区：裁剪工具结果内容
  acp::ACPMessage pruned_msg;
  pruned_msg.set_role(msg.role());

  for (const auto& block : msg.content()) {
   if (block.is_tool_result()) {
    const auto& tr = block.tool_result();
    auto output_len = tr.output.size();

    if (output_len > static_cast<size_t>(opts.max_tool_result_chars)) {
     // 软裁剪
     soft_pruned++;
     log::debug_fmt("context_pruner: msg[{}] soft prune, depth={}, tool_id={}, orig_len={}",
                   idx, nearest_depth,
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
  int depth = depths[idx];

  if (depth > 0 && depth <= opts.protect_recent) {
   result.push_back(msg);
   continue;
  }

  // 剥离区：剥离 tool_use 块，纯 tool_use → 摘要替代
  if (depth > opts.hard_prune_after) {
   acp::ACPMessage pruned_msg;
   pruned_msg.set_role(msg.role());
   container::Vector<container::String> tool_names;

   for (const auto& block : msg.content()) {
    if (block.is_text() && !block.is_thinking()) {
     pruned_msg.add_content(block);
    } else if (block.is_tool_use()) {
     tool_names.push_back(block.tool_use().name);
     stripped_uses++;
    }
    // tool_result 块在剥离区也删除
   }

   // 如果剥离后无 text，生成摘要替代
   if (pruned_msg.content().empty() && !tool_names.empty()) {
    std::string summary = "[used tools: ";
    for (size_t i = 0; i < tool_names.size(); ++i) {
     if (i > 0) summary += ", ";
     summary += std::string(tool_names[i].data(), tool_names[i].size());
    }
    summary += "]";
    pruned_msg.add_text(container::String(summary.c_str()));
   }

   // 只有有内容时才加入结果（避免空消息）
   if (!pruned_msg.content().empty()) {
    result.push_back(std::move(pruned_msg));
   }
   continue;
  }

  // 软裁剪区：保留 tool_use，软裁剪 tool result
  acp::ACPMessage pruned_msg;
  pruned_msg.set_role(msg.role());

  for (const auto& block : msg.content()) {
   if (block.is_tool_result()) {
    const auto& tr = block.tool_result();

    if (tr.output.size() > static_cast<size_t>(opts.max_tool_result_chars)) {
     soft_pruned++;
     log::debug_fmt("context_pruner: msg[{}] assistant inline soft prune, depth={}, tool_id={}",
                   idx, depth,
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

 return PruneResult{std::move(result), hard_pruned, soft_pruned, stripped_msgs, stripped_uses};
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
