/**
 * @file workflow_example.cpp
 * @brief BenGear Workflow 使用示例
 * 
 * 本示例展示如何使用 BenGear Workflow 模块构建和执行工作流：
 * 1. 简单的线性工作流
 * 2. 并行任务执行
 * 3. 复杂的 DAG 工作流
 * 4. 错误处理和重试
 */

#include "ben_gear/workflow/workflow_builder.hpp"
#include "ben_gear/workflow/workflow_runner.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include <iostream>
#include <chrono>

using namespace ben_gear::workflow;
using namespace ben_gear::base::concurrency;

// ==================== 示例 1: 简单的线性工作流 ====================
void example_simple_linear_workflow() {
    std::cout << "\n=== Example 1: Simple Linear Workflow ===\n";
    
    // 创建共享线程池
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    // 构建工作流：task1 -> task2 -> task3
    WorkflowBuilder builder;
    
    builder.add_task("task1", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Task1] Generating data...\n";
        return TaskResult::ok(std::string("Hello"));
    });
    
    builder.add_task("task2", [](const TaskContext& ctx) {
        auto data = ctx.get_upstream_result<std::string>("task1");
        std::cout << "  [Task2] Processing: " << *data << "\n";
        return TaskResult::ok(*data + " World");
    });
    
    builder.add_task("task3", [](const TaskContext& ctx) {
        auto data = ctx.get_upstream_result<std::string>("task2");
        std::cout << "  [Task3] Final result: " << *data << "\n";
        return TaskResult::ok(*data + "!");
    });
    
    builder.add_dependency("task1", "task2");
    builder.add_dependency("task2", "task3");
    
    // 执行工作流
    auto result = runner->run(builder.build());
    
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Tasks completed: " << result.completed_tasks << "/" << result.total_tasks << "\n";
}

// ==================== 示例 2: 并行任务执行 ====================
void example_parallel_tasks() {
    std::cout << "\n=== Example 2: Parallel Task Execution ===\n";
    
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    WorkflowBuilder builder;
    
    // 三个独立的任务可以并行执行
    builder.add_task("fetch_user", [](const TaskContext& /*ctx*/) {
        std::cout << "  [FetchUser] Fetching user data...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TaskResult::ok(std::string("User: Alice"));
    });
    
    builder.add_task("fetch_orders", [](const TaskContext& /*ctx*/) {
        std::cout << "  [FetchOrders] Fetching orders...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TaskResult::ok(std::vector<int>{1, 2, 3});
    });
    
    builder.add_task("fetch_products", [](const TaskContext& /*ctx*/) {
        std::cout << "  [FetchProducts] Fetching products...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TaskResult::ok(std::vector<std::string>{"Product A", "Product B"});
    });
    
    // 汇总任务（依赖前面三个任务）
    builder.add_task("aggregate", [](const TaskContext& ctx) {
        auto user = ctx.get_upstream_result<std::string>("fetch_user");
        auto orders = ctx.get_upstream_result<std::vector<int>>("fetch_orders");
        auto products = ctx.get_upstream_result<std::vector<std::string>>("fetch_products");
        
        std::cout << "  [Aggregate] User: " << *user 
                  << ", Orders: " << orders->size()
                  << ", Products: " << products->size() << "\n";
        
        return TaskResult::ok();
    });
    
    builder.add_dependency("fetch_user", "aggregate");
    builder.add_dependency("fetch_orders", "aggregate");
    builder.add_dependency("fetch_products", "aggregate");
    
    // 执行并计时
    auto start = std::chrono::steady_clock::now();
    auto result = runner->run(builder.build());
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Execution time: " << elapsed << "ms (parallel execution should be ~100ms)\n";
}

// ==================== 示例 3: 复杂的 DAG 工作流 ====================
void example_complex_dag() {
    std::cout << "\n=== Example 3: Complex DAG Workflow ===\n";
    
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    WorkflowBuilder builder;
    
    // 构建复杂的 DAG：
    //       init
    //      /    |
    //   load_A  load_B
    //      |    /  |
    //      process  validate_B
    //        |       |
    //      output  report
    
    builder.add_task("init", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Init] Initializing...\n";
        return TaskResult::ok();
    });
    
    builder.add_task("load_A", [](const TaskContext& /*ctx*/) {
        std::cout << "  [LoadA] Loading data A...\n";
        return TaskResult::ok(std::vector<int>{1, 2, 3});
    });
    
    builder.add_task("load_B", [](const TaskContext& /*ctx*/) {
        std::cout << "  [LoadB] Loading data B...\n";
        return TaskResult::ok(std::vector<int>{4, 5, 6});
    });
    
    builder.add_task("process", [](const TaskContext& ctx) {
        auto data_a = ctx.get_upstream_result<std::vector<int>>("load_A");
        auto data_b = ctx.get_upstream_result<std::vector<int>>("load_B");
        
        std::vector<int> merged;
        merged.insert(merged.end(), data_a->begin(), data_a->end());
        merged.insert(merged.end(), data_b->begin(), data_b->end());
        
        std::cout << "  [Process] Merged " << merged.size() << " items\n";
        return TaskResult::ok(merged);
    });
    
    builder.add_task("validate_B", [](const TaskContext& ctx) {
        auto data_b = ctx.get_upstream_result<std::vector<int>>("load_B");
        std::cout << "  [ValidateB] Validating " << data_b->size() << " items\n";
        return TaskResult::ok();
    });
    
    builder.add_task("output", [](const TaskContext& ctx) {
        auto merged = ctx.get_upstream_result<std::vector<int>>("process");
        std::cout << "  [Output] Writing " << merged->size() << " items\n";
        return TaskResult::ok();
    });
    
    builder.add_task("report", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Report] Generating report...\n";
        return TaskResult::ok();
    });
    
    // 添加依赖
    builder.add_dependency("init", "load_A");
    builder.add_dependency("init", "load_B");
    builder.add_dependency("load_A", "process");
    builder.add_dependency("load_B", "process");
    builder.add_dependency("load_B", "validate_B");
    builder.add_dependency("process", "output");
    builder.add_dependency("validate_B", "report");
    
    // 执行工作流
    auto result = runner->run(builder.build());
    
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Tasks completed: " << result.completed_tasks << "/" << result.total_tasks << "\n";
}

// ==================== 示例 4: 错误处理和重试 ====================
void example_error_handling() {
    std::cout << "\n=== Example 4: Error Handling and Retry ===\n";
    
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    // 设置错误处理策略
    runner->set_error_strategy(ErrorHandlingStrategy::FAIL_FAST);
    
    WorkflowBuilder builder;
    
    // 任务1：成功
    builder.add_task("task1", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Task1] Success\n";
        return TaskResult::ok();
    });
    
    // 任务2：失败
    builder.add_task("task2", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Task2] Failing intentionally\n";
        return TaskResult::error("Intentional failure");
    });
    
    // 任务3：依赖任务2（不会执行）
    builder.add_task("task3", [](const TaskContext& /*ctx*/) {
        std::cout << "  [Task3] Should not execute\n";
        return TaskResult::ok();
    });
    
    builder.add_dependency("task1", "task2");
    builder.add_dependency("task2", "task3");
    
    // 执行工作流
    auto result = runner->run(builder.build());
    
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Error: " << result.error_message << "\n";
    std::cout << "Tasks completed: " << result.completed_tasks << "/" << result.total_tasks << "\n";
}

// ==================== 示例 5: 使用 Agent 形式的任务 ====================
void example_agent_tasks() {
    std::cout << "\n=== Example 5: Agent-based Tasks ===\n";
    
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    WorkflowBuilder builder;
    
    // 使用 Agent 形式添加任务
    builder.add_task("research", "researcher", "Search for information about C++20 coroutines", 600);
    builder.add_task("code", "coder", "Implement a simple coroutine example", 600);
    builder.add_task("review", "reviewer", "Review the code for best practices", 600);
    
    builder.add_dependency("research", "code");
    builder.add_dependency("code", "review");
    
    // 执行工作流
    auto result = runner->run(builder.build());
    
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Tasks completed: " << result.completed_tasks << "/" << result.total_tasks << "\n";
}

// ==================== 示例 6: 查询工作流状态 ====================
void example_workflow_status() {
    std::cout << "\n=== Example 6: Workflow Status ===\n";
    
    auto thread_pool = std::make_shared<ThreadPool>();
    auto runner = std::make_shared<WorkflowRunner>(thread_pool);
    
    WorkflowBuilder builder;
    
    builder.add_task("long_task", [](const TaskContext& /*ctx*/) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return TaskResult::ok();
    });
    
    // 异步执行
    auto future = runner->run_async(builder.build());
    
    // 查询状态
    for (int i = 0; i < 3; ++i) {
        auto status = runner->get_status();
        std::cout << "  Status: running=" << status.running 
                  << ", completed=" << status.completed_tasks 
                  << "/" << status.total_tasks << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待完成
    auto result = future.get();
    std::cout << "Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
}

// ==================== Main ====================
int main() {
    std::cout << "========================================\n";
    std::cout << "  BenGear Workflow Examples\n";
    std::cout << "========================================\n";
    
    example_simple_linear_workflow();
    example_parallel_tasks();
    example_complex_dag();
    example_error_handling();
    example_agent_tasks();
    example_workflow_status();
    
    std::cout << "\n========================================\n";
    std::cout << "  All examples completed!\n";
    std::cout << "========================================\n";
    
    return 0;
}
