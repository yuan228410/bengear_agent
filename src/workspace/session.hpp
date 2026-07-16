#pragma once

#include "base/net/io_context.hpp"
#include "memory/store.hpp"
#include "llm/conversation_history.hpp"
#include "llm/provider_client.hpp"
#include "llm/usage.hpp"
#include "capabilities/tool/registry.hpp"
#include "workspace/types.hpp"
#include "base/utils/uuid.hpp"
#include "workspace/history_db.hpp"
#include "base/utils/json.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::memory { class Compactor; class MemoryUpdater; class EpisodeStore; }

namespace ben_gear::workspace {

namespace container = base::container;

/// 会话类 — 隔离单元
class Session {
public:
    /// 构造会话
    /// session_type=sub_agent 时跳过情景工具注册和会话目录创建
    explicit Session(SessionConfig config, SessionDeps deps,
                     capabilities::tool::ToolRegistry& tools);
    ~Session();

    /// 独占资源
    llm::ConversationHistory& history() { return history_; }
    const llm::ConversationHistory& history() const { return history_; }

    /// 元数据
    const std::string& session_id() const { return session_id_; }
    const WorkspaceContext& workspace_context() const { return ws_ctx_; }
    const std::filesystem::path& session_dir() const { return session_dir_; }
    memory::MemoryStore& memory_store() { return *memory_store_; }
    const memory::MemoryStore& memory_store() const { return *memory_store_; }
    const std::shared_ptr<memory::EpisodeStore>& episode_store() const {
        return episode_store_;
    }

    /// 会话类型和父会话
    agent::SessionType session_type() const { return session_type_; }
    const std::string& parent_session_id() const { return parent_session_id_; }

    /// 压缩检查
    void maybe_compact(net::EventLoop& loop,
                       llm::ProviderClient& provider,
                       const capabilities::tool::ToolRegistry& tools);

    /// 强制压缩
    bool force_compact(net::EventLoop& loop,
                       llm::ProviderClient& provider,
                       const capabilities::tool::ToolRegistry& tools,
                       int max_compact_calls = 5);

    /// 通用消息持久化
    void persist_message(const std::string& role,
                         const std::string& content,
                         workspace::HistoryDB& db);

    /// 持久化 assistant 消息 + 工具调用
    void persist_assistant_message(
        const std::string& content,
        const std::vector<capabilities::tool::ToolCallRequest>& tool_calls,
        workspace::HistoryDB& db);

    /// 持久化带工具调用的 assistant 消息
    void persist_assistant_with_tools(
        const std::string& content,
        const std::vector<capabilities::tool::ToolCallRequest>& tool_calls,
        workspace::HistoryDB& db);

    /// 持久化工具结果
    void persist_tool_result(const std::string& tool_call_id,
                             const std::string& tool_name,
                             const std::string& content,
                             workspace::HistoryDB& db);

    /// 恢复会话历史
    void restore_from_db(workspace::HistoryDB& db);

private:
    std::string session_id_;
    WorkspaceContext ws_ctx_;
    std::filesystem::path session_dir_;
    agent::SessionType session_type_ = agent::SessionType::main;
    std::string parent_session_id_;

    // 独占资源
    llm::ConversationHistory history_;
    config::ContextPruneSettings prune_config_;
    std::unique_ptr<memory::Compactor> compactor_;
    std::unique_ptr<memory::MemoryUpdater> memory_updater_;
    std::shared_ptr<memory::EpisodeStore> episode_store_;

    // 共享资源
    std::shared_ptr<memory::MemoryStore> memory_store_;
};

}  // namespace ben_gear::workspace
