#pragma once

#include <memory>

// 前向声明：仅用于 shared_ptr，完整类型在 runtime.hpp 引入
namespace ben_gear {

namespace base::concurrency { class ThreadPool; }
namespace net { class IoContext; }

namespace workspace_index { class WorkspaceIndexService; }
namespace repo_map { class RepoMapService; }
namespace code_intel { class CodeIntelService; }

namespace application { class WorkspaceResolver; }

}  // namespace ben_gear

namespace ben_gear::agent::runtime {

/// 代码智能服务束
struct IntelligenceServices {
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index;
    std::shared_ptr<repo_map::RepoMapService> repo_map;
    std::shared_ptr<code_intel::CodeIntelService> code_intel;
};

/// 基础设施服务束
struct InfrastructureServices {
    std::shared_ptr<base::concurrency::ThreadPool> core_pool;
    std::shared_ptr<net::IoContext> io_context;
    std::shared_ptr<net::IoContext> wf_context;
    std::shared_ptr<net::IoContext> util_context;
};

}  // namespace ben_gear::agent::runtime
