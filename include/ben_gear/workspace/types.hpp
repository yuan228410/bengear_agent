#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/tier_paths.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/context.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/agent/sub_agent_config.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

using Tier = base::Tier;
using TierPaths = base::TierPaths;

/// 工作空间元数据
struct WorkspaceMeta {
 container::String name;
 container::String project_path;
 std::filesystem::path ws_dir;
 bool deleted = false;
};

/// 会话元数据
struct SessionMeta {
 container::String session_id;
 container::String workspace_name;
 container::String name;
 std::filesystem::path session_dir;
 std::string created_at;
 std::string updated_at;
 agent::SessionType session_type = agent::SessionType::main;
 container::String parent_session_id;
};

/// 会话配置（用户可配置参数）
struct SessionConfig {
 container::String session_id;
 int64_t context_length = 0;
 config::ContextPruneSettings context_prune;
 agent::SessionType session_type = agent::SessionType::main;
 container::String parent_session_id;
};

/// 工作空间上下文
struct WorkspaceContext {
 TierPaths tier_paths;
 container::String workspace_name;
 container::String project_path;
 container::String username;
 container::String session_id;
};

/// 会话依赖的基础设施
struct SessionDeps {
 WorkspaceContext ws_ctx;
 std::shared_ptr<memory::MemoryStore> memory_store;
 const memory::ContextBuilder* context_builder = nullptr;
 std::shared_ptr<base::concurrency::ThreadPool> thread_pool;
};

} // namespace ben_gear::workspace
