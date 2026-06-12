#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <string>

namespace ben_gear::memory {

namespace container = base::container;
namespace acp = ben_gear::acp;

/// 上下文裁剪器 — 三级策略裁剪旧工具结果，减少 prompt token
///
/// - protect_recent: 最近 N 轮助手消息的工具结果完整保留
/// - soft_prune: 旧结果截断为首尾几行 + 省略号
/// - hard_prune: 很旧的结果整条删除，assistant 剥离 tool_use 块（纯 tool_use → 摘要替代）
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
 /// 裁剪结果（不含 token 估算，由调用方统一管理）
 struct PruneResult {
  container::Vector<acp::ACPMessage> messages;
  int hard_pruned = 0;
  int soft_pruned = 0;
  int stripped_msgs = 0;  // 整条删除的 tool result 消息数
  int stripped_uses = 0;  // assistant 剥离的 tool_use 块数
 };

 /// 裁剪消息历史中的工具结果，返回新列表
 static PruneResult prune(
  const container::Vector<acp::ACPMessage>& history,
  const Options& opts = Options());

 /// 计算每个助手消息的 depth（从最新往回编号，非助手返回 -1）
 static container::Vector<int> compute_depths(
  const container::Vector<acp::ACPMessage>& history);

 /// 对 [start, end) 范围的消息做裁剪（使用预计算的全量 depth 数组）
 /// 用于增量裁剪：冻结区跳过，仅重算活跃区
 static PruneResult prune_range_with_depths(
  const container::Vector<acp::ACPMessage>& history,
  size_t start,
  const container::Vector<int>& depths,
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
