#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/llm/usage.hpp"
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
class Session {
public:
    /// 构造会话
    /// session_type=sub_agent 时跳过情景工具注册和会话目录创建
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

    /// 会话类型和父会话
    agent::SessionType session_type() const { return session_type_; }
    const container::String& parent_session_id() const { return parent_session_id_; }

    /// 压缩检查
    void maybe_compact(net::EventLoop& loop,
                       llm::ProviderClient& provider,
                       const llm::ToolRegistry& tools);

    /// 强制压缩
    bool force_compact(net::EventLoop& loop,
                       llm::ProviderClient& provider,
                       const llm::ToolRegistry& tools,
                       int max_compact_calls = 5);

    /// 持久化用户消息
    void persist_user_message(const container::String& content,
                              workspace::HistoryDB& db);

    /// 通用消息持久化
    void persist_message(const container::String& role,
                         const container::String& content,
                         workspace::HistoryDB& db);

    /// 持久化 assistant 消息 + 工具调用
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
    agent::SessionType session_type_ = agent::SessionType::main;
    container::String parent_session_id_;

    // 独占资源
    workspace::ConversationHistory history_;
    std::unique_ptr<memory::Compactor> compactor_;
    std::unique_ptr<memory::MemoryUpdater> memory_updater_;
    std::shared_ptr<memory::EpisodeStore> episode_store_;

    // 共享资源
    std::shared_ptr<memory::MemoryStore> memory_store_;
};

}  // namespace ben_gear::workspace
