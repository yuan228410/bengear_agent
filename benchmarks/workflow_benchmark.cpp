/// 工作流引擎 benchmark
/// 测试：工作流注册、命名空间隔离、DAG 构建性能

#include "ben_gear/workflow/workflow_engine.hpp"
#include <chrono>
#include <iostream>
#include <vector>

using namespace ben_gear::workflow;
using namespace std::chrono;

/// 生成 N 个并行任务的工作流
WorkflowDefinition make_parallel_workflow(int n) {
    WorkflowDefinition wf;
    wf.id = "parallel_" + std::to_string(n);
    wf.name = "Parallel " + std::to_string(n);
    for (int i = 0; i < n; ++i) {
        WorkflowTaskDefinition task;
        task.id = "task_" + std::to_string(i);
        task.type = "function";
        task.prompt = "Task " + std::to_string(i);
        wf.tasks.push_back(task);
    }
    return wf;
}

/// 生成 N 个串行任务的工作流
WorkflowDefinition make_sequential_workflow(int n) {
    WorkflowDefinition wf;
    wf.id = "sequential_" + std::to_string(n);
    wf.name = "Sequential " + std::to_string(n);
    for (int i = 0; i < n; ++i) {
        WorkflowTaskDefinition task;
        task.id = "task_" + std::to_string(i);
        task.type = "function";
        task.prompt = "Task " + std::to_string(i);
        if (i > 0) task.depends_on = {"task_" + std::to_string(i - 1)};
        wf.tasks.push_back(task);
    }
    return wf;
}

int main() {
    // TaskExecutor no longer uses ThreadPool
    auto engine = std::make_shared<WorkflowEngine>();

    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   Workflow Engine Benchmark            ║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";

    // Benchmark 1: 注册工作流（1000 次）
    {
        auto start = high_resolution_clock::now();
        for (int i = 0; i < 1000; ++i) {
            auto wf = make_parallel_workflow(5);
            engine->register_workflow(wf, "bench_ns");
        }
        auto end = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(end - start).count();
        std::cout << "Register 1000 workflows (5 tasks each): " << ms << "ms\n";
    }

    // Benchmark 2: 命名空间隔离查询
    {
        // 注册到不同命名空间
        for (int i = 0; i < 100; ++i) {
            auto wf = make_parallel_workflow(3);
            engine->register_workflow(wf, "ns_" + std::to_string(i));
        }
        auto start = high_resolution_clock::now();
        for (int i = 0; i < 100; ++i) {
            engine->list_workflows("ns_" + std::to_string(i));
        }
        auto end = high_resolution_clock::now();
        auto ms = duration_cast<microseconds>(end - start).count();
        std::cout << "List workflows by namespace (100x): " << ms << "us\n";
    }

    // Benchmark 3: 验证工作流定义
    {
        auto wf = make_parallel_workflow(50);
        auto start = high_resolution_clock::now();
        for (int i = 0; i < 1000; ++i) {
            engine->validate_workflow(wf);
        }
        auto end = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(end - start).count();
        std::cout << "Validate 1000 workflows (50 tasks each): " << ms << "ms\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
