#pragma once

#include "agent/execution/interceptor.hpp"
#include "memory/compactor.hpp"
#include "memory/updater.hpp"

#include <functional>
#include <string_view>

namespace ben_gear::llm { class ProviderClient; }
namespace ben_gear::capabilities::tool { class ToolRegistry; }
namespace ben_gear::net { class EventLoop; }

namespace ben_gear::agent::execution {

/// 上下文压缩拦截器
///
/// 在每次 LLM 调用前检查上下文长度，若超过软阈值则触发压缩。
/// 同时暴露 force_compact() 供外部在 LLM 返回 context_overflow 后强制恢复。
///
/// 用法：CompactionInterceptor 内部依赖 EventLoop + ProviderClient +
/// ToolRegistry 来执行压缩所需的 LLM 摘要调用。这些由 Runtime 在构造时注入。
class CompactionInterceptor : public IInterceptor {
public:
    using ChatFn = std::function<std::string(const std::string&)>;

    /// @param compactor  压缩器（由 Session 或 Runtime 共享所有权）
    /// @param updater    记忆更新器（可选，nullptr 跳过记忆更新）
    /// @param chat_fn    用于压缩摘要的 LLM 调用闭包（已绑定 loop/provider/tools）
    CompactionInterceptor(memory::Compactor* compactor,
                          memory::MemoryUpdater* updater,
                          ChatFn chat_fn);

    const char* name() const noexcept override { return "Compaction"; }

    /// LLM 调用前：检查是否需要软压缩
    void before_llm(llm::ConversationHistory& history,
                    LoopSnapshot& ctx) override;

    /// 上下文溢出后的强制恢复（由 ExecutionLoop 在检测到 overflow 时调用）
    /// @return true 表示恢复成功，可继续；false 表示无法恢复
    bool force_compact(llm::ConversationHistory& history);

    /// 收集压缩前的轮次摘要（供 MemoryUpdater 使用）
    void collect_summaries(const llm::ConversationHistory& history);

private:
    memory::Compactor* compactor_;
    memory::MemoryUpdater* updater_;
    ChatFn chat_fn_;
    std::vector<std::string> round_summaries_;
    bool summaries_collected_ = false;
};

}  // namespace ben_gear::agent::execution
