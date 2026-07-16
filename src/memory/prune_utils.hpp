#pragma once

// fwd
namespace ben_gear::llm { class ConversationHistory; }
namespace ben_gear::config { struct ContextPruneSettings; }

#include <cstdint>
#include <vector>
namespace ben_gear::memory {

/// 上下文裁剪工具 — 自由函数，操作 llm::ConversationHistory
///
/// 设计原因：裁剪逻辑依赖 memory::ContextPruner，不能放在 llm 模块中
/// （否则 llm → memory → llm 形成循环）。此处作为桥接层，由 memory 模块
/// 提供裁剪能力，llm 模块的 ConversationHistory 保持纯粹的消息容器。
struct PruneUtils {

    /// 对历史执行上下文裁剪（原地修改）
    /// @return 裁剪掉的 token 数（原始 - 裁剪后）
    static int64_t apply_prune(llm::ConversationHistory& history,
                               const config::ContextPruneSettings& cfg);

    /// 估算历史消息的 token 数
    static int64_t estimate_tokens(const llm::ConversationHistory& history);
};

}  // namespace ben_gear::memory
