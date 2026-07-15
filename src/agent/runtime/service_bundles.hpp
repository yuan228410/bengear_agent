#pragma once

#include <memory>

// 前向声明：仅用于 shared_ptr，完整类型在 runtime.hpp 引入
namespace ben_gear {

namespace base::concurrency { class ThreadPool; }
namespace net { class IoContext; }

namespace permission { class PolicyEngine; }
namespace patch { class PatchService; }
namespace git { class GitService; }
namespace checkpoint { class CheckpointService; }
namespace test_loop { class TestLoopService; }

namespace workspace_index { class WorkspaceIndexService; }
namespace repo_map { class RepoMapService; }
namespace code_intel { class CodeIntelService; }
namespace diagnostic_context { class DiagnosticContextService; }
namespace diagnostic_repair {
    class DiagnosticRepairPlanService;
    class DiagnosticRepairPatchPreviewService;
}

namespace application { class WorkspaceResolver; class PatchUseCases; }

}  // namespace ben_gear

namespace ben_gear::agent::runtime {

/// 安全变更服务束
/// 聚合所有安全相关的能力：权限、补丁、Git、检查点、测试循环
struct SafeChangeServices {
    std::shared_ptr<permission::PolicyEngine> policy_engine;
    std::shared_ptr<patch::PatchService> patch_service;
    std::shared_ptr<application::WorkspaceResolver> patch_workspace_resolver;
    std::shared_ptr<application::PatchUseCases> patch_use_cases;
    std::shared_ptr<git::GitService> git_service;
    std::shared_ptr<checkpoint::CheckpointService> checkpoint_service;
    std::shared_ptr<test_loop::TestLoopService> test_loop_service;
};

/// 代码智能服务束
/// 聚合工作空间索引、仓库地图、代码智能、诊断上下文和修复
struct IntelligenceServices {
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index;
    std::shared_ptr<repo_map::RepoMapService> repo_map;
    std::shared_ptr<code_intel::CodeIntelService> code_intel;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> diagnostic_context;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> diagnostic_repair_plan;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> diagnostic_repair_preview;
};

/// 基础设施服务束
/// 聚合线程池和 I/O 上下文
struct InfrastructureServices {
    std::shared_ptr<base::concurrency::ThreadPool> core_pool;
    std::shared_ptr<net::IoContext> io_context;
    std::shared_ptr<net::IoContext> wf_context;
    std::shared_ptr<net::IoContext> util_context;
};

}  // namespace ben_gear::agent::runtime
