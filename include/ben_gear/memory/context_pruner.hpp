#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::memory {

namespace container = base::container;
namespace acp = ben_gear::acp;

/// 上下文裁剪器 — 三级策略裁剪旧工具结果，减少 prompt token
///
/// - protect_recent: 最近 N 轮助手消息的工具结果完整保留
/// - soft_prune: 旧结果截断为首尾几行 + 省略号
/// - hard_prune: 很旧的结果替换为占位符
///
/// 不修改原始 history，返回裁剪后的新消息列表
class ContextPruner {
public:
 /// 裁剪选项
 struct Options {
  int protect_recent;
  int soft_prune_lines;
  int hard_prune_after;
  int max_tool_result_chars;

  Options()
   : protect_recent(3)
   , soft_prune_lines(5)
   , hard_prune_after(10)
   , max_tool_result_chars(2000) {}
 };

 /// 裁剪消息历史中的工具结果，返回新列表
 static container::Vector<acp::ACPMessage> prune(
  const container::Vector<acp::ACPMessage>& history,
  const Options& opts = Options());

 /// 估算消息的 token 数（4 字符 ≈ 1 token，CJK 感知）
 static int64_t estimate_tokens(const acp::ACPMessage& msg);
 static int64_t estimate_tokens(const container::Vector<acp::ACPMessage>& msgs);

private:
 /// 软裁剪：保留首尾 N 行 + 省略号
 static container::String soft_prune(const container::String& content, int keep_lines);

 /// 是否为过期的工具权限错误（LLM 应重试）
 static bool is_stale_tool_error(const container::String& content);
};

} // namespace ben_gear::memory
