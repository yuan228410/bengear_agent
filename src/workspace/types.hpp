#pragma once

#include "workspace/workspace_types.hpp"
#include "concurrency/thread_pool.hpp"
#include "config/sub_agent_config.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace ben_gear::memory { class MemoryStore; class ContextBuilder; }
namespace ben_gear::workspace { class HistoryDB; }

namespace ben_gear::workspace {

/// 会话元数据
struct SessionMeta {
    std::string session_id;
    std::string workspace_name;
    std::string name;
    std::string created_at;
    std::string updated_at;
    config::SessionType session_type = config::SessionType::main;
    std::string parent_session_id;
};

/// 会话配置（用户可配置参数）
struct SessionConfig {
    std::string session_id;
    int64_t context_length = 0;
    config::ContextPruneSettings context_prune;
    config::SessionType session_type = config::SessionType::main;
    std::string parent_session_id;
};

/// 会话依赖的基础设施
struct SessionDeps {
    WorkspaceContext ws_ctx;
    std::shared_ptr<memory::MemoryStore> memory_store;
    const memory::ContextBuilder* context_builder = nullptr;
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool;
    HistoryDB* history_db = nullptr;  // 用于 EpisodeStore
};

} // namespace ben_gear::workspace
