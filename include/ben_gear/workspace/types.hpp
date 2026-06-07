#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/tier_paths.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/context.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

// 从 base 层导入，保持 workspace::TierPaths / workspace::Tier 兼容
using Tier     = base::Tier;
using TierPaths = base::TierPaths;

/// 工作空间元数据
struct WorkspaceMeta {
    container::String name;
    container::String project_path;   // 关联的项目路径
    std::filesystem::path ws_dir;     // 工作空间数据目录
    bool deleted = false;             // 软删除标记
};

/// 会话元数据
struct SessionMeta {
    container::String session_id;     // UUID v4
    container::String workspace_name;
    container::String name;           // 可选的会话名称
    std::filesystem::path session_dir;
    std::string created_at;           // ISO 8601
    std::string updated_at;
};

/// 会话配置（用户可配置参数）
struct SessionConfig {
    container::String session_id;   // 空=自动生成 UUID
    int64_t context_length = 0;     // 0=使用默认 256000
    // 未来可扩展：temperature, max_tokens, model 等
};

/// 工作空间上下文（传递给 Agent / Session）
struct WorkspaceContext {
    TierPaths tier_paths;
    container::String workspace_name;
    container::String username;
    container::String session_id;     // 当前活跃会话，空=新建
};

/// 会话依赖的基础设施（由 SharedResources 填充，解耦 workspace→agent 依赖）
/// EpisodeStore 由 Session 自行创建（session_dir 在 Session 构造时才确定）
struct SessionDeps {
    WorkspaceContext ws_ctx;
    std::shared_ptr<memory::MemoryStore> memory_store;
    const memory::ContextBuilder* context_builder = nullptr;  // 非拥有指针，生命周期由 SharedResources 保证
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool;  // 核心调度线程池，工作流引擎使用
};

}  // namespace ben_gear::workspace
