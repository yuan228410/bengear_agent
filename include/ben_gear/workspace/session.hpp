#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/memory/compactor.hpp"
#include "ben_gear/memory/updater.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/tools/memory_tools.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

/// 会话类 — 隔离单元
/// 每个 Session 独占 ConversationHistory、Compactor、MemoryUpdater、EpisodeStore
/// EventLoop 由 SharedResources 的 IoContext 统一管理，Session 不持有
/// 多个 Session 之间不共享可变状态，无需加锁
class Session {
public:
    /// 构造会话，依赖通过 SessionDeps 注入
    /// tools 参数：由 SharedResources 持有的工具注册表，Session 在其上追加情景工具
    /// 注意：tools 必须有效，且生命周期长于 Session（由 SharedResources 保证）
    ///
    /// 线程安全说明：
    /// - Session 构造时会修改 ToolRegistry（注册情景记忆工具）
    /// - 调用方需确保同一时刻只有一个 Session 在构造（由 Agent 保证）
    explicit Session(SessionConfig config, SessionDeps deps,
                     llm::ToolRegistry& tools);

    /// 独占资源
    workspace::ConversationHistory& history() { return history_; }
    const workspace::ConversationHistory& history() const { return history_; }

    /// 元数据
    const container::String& session_id() const { return session_id_; }
    const WorkspaceContext& workspace_context() const { return ws_ctx_; }
    const std::filesystem::path& session_dir() const { return session_dir_; }
    memory::MemoryStore& memory_store() { return *memory_store_; }
    const memory::MemoryStore& memory_store() const { return *memory_store_; }
    const std::shared_ptr<memory::EpisodeStore>& episode_store() const {
        return episode_store_;
    }

    /// 压缩检查（会话级状态，独占）
    void maybe_compact(net::EventLoop& loop,
                       llm::ProviderClient& provider,
                       const llm::ToolRegistry& tools);

    /// 持久化用户消息
    void persist_user_message(const container::String& content,
                              workspace::HistoryDB& db);

    /// 通用消息持久化
    void persist_message(const container::String& role,
                         const container::String& content,
                         workspace::HistoryDB& db);

    /// 持久化 assistant 消息 + 工具调用（每条工具调用独立行）
    void persist_assistant_message(
        const container::String& content,
        const std::vector<llm::ToolCallRequest>& tool_calls,
        workspace::HistoryDB& db);

    /// 持久化带工具调用的 assistant 消息
    void persist_assistant_with_tools(
        const container::String& content,
        const std::vector<llm::ToolCallRequest>& tool_calls,
        workspace::HistoryDB& db);

    /// 持久化工具结果
    void persist_tool_result(const container::String& tool_call_id,
                             const container::String& tool_name,
                             const container::String& content,
                             workspace::HistoryDB& db);

    /// 恢复会话历史
    void restore_from_db(workspace::HistoryDB& db);

private:
    container::String session_id_;
    WorkspaceContext ws_ctx_;
    std::filesystem::path session_dir_;

    // 独占资源（每个 Session 一份，不共享）
    workspace::ConversationHistory history_;
    std::unique_ptr<memory::Compactor> compactor_;
    std::unique_ptr<memory::MemoryUpdater> memory_updater_;
    std::shared_ptr<memory::EpisodeStore> episode_store_;

    // 共享资源（通过 shared_ptr 共享所有权）
    std::shared_ptr<memory::MemoryStore> memory_store_;
};

}  // namespace ben_gear::workspace
