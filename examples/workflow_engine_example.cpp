/**
 * @file workflow_engine_example.cpp
 * @brief WorkflowEngine 完整使用示例
 * 
 * 本示例展示如何使用 WorkflowEngine 和模板库：
 * 1. 初始化工作流系统
 * 2. 使用内置模板
 * 3. 动态创建工作流
 * 4. 执行和监控
 * 5. 获取性能指标
 */

#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/workflow/workflow_templates.hpp"
#include "ben_gear/workflow/metrics.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include <iostream>

using namespace ben_gear::workflow;
using namespace ben_gear::base::concurrency;

// ==================== 示例 1: 使用内置模板 ====================
void example_builtin_template() {
    std::cout << "\n=== Example 1: Using Built-in Templates ===\n";
    
    // 创建模板库
    auto template_lib = std::make_shared<WorkflowTemplateLibrary>();
    
    // 注册内置模板
    template_lib->register_template(templates::code_review());
    template_lib->register_template(templates::documentation());
    
    // 列出所有模板
    auto template_names = template_lib->list();
    std::cout << "Available templates:\n";
    for (const auto& name : template_names) {
        auto tmpl = template_lib->get(name);
        if (tmpl) {
            std::cout << "  - " << name << ": " << tmpl->name 
                      << " (" << tmpl->tasks.size() << " tasks)\n";
        }
    }
    
    // 实例化模板
    try {
        auto workflow = template_lib->instantiate("code_review", {
            {"project_path", "/Users/yuanzhixiang/yzx_code/my_agent"}
        });
        
        std::cout << "\nInstantiated workflow:\n";
        std::cout << "  ID: " << workflow.id << "\n";
        std::cout << "  Name: " << workflow.name << "\n";
        std::cout << "  Tasks: " << workflow.tasks.size() << "\n";
        
        for (const auto& task : workflow.tasks) {
            std::cout << "    - " << task.id << " (" << task.type << ")";
            if (!task.depends_on.empty()) {
                std::cout << " depends on: ";
                for (const auto& dep : task.depends_on) {
                    std::cout << dep << " ";
                }
            }
            std::cout << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

// ==================== 示例 2: 动态创建工作流 ====================
void example_dynamic_workflow() {
    std::cout << "\n=== Example 2: Dynamic Workflow Creation ===\n";
    
    // 创建工作流定义
    WorkflowDefinition workflow;
    workflow.id = "custom_workflow";
    workflow.name = "Custom Analysis Workflow";
    
    // 添加任务
    workflow.tasks.push_back({
        .id = "fetch_data",
        .name = "Fetch Data",
        .type = "function",
        .prompt = "Fetching data from API...",
        .depends_on = {},
        .config = Json::object()
    });
    
    workflow.tasks.push_back({
        .id = "process_data",
        .name = "Process Data",
        .type = "function",
        .prompt = "Processing data: {fetch_data}",
        .depends_on = {"fetch_data"},
        .config = Json::object()
    });
    
    workflow.tasks.push_back({
        .id = "generate_report",
        .name = "Generate Report",
        .type = "function",
        .prompt = "Generating report based on: {process_data}",
        .depends_on = {"process_data"},
        .config = Json::object()
    });
    
    std::cout << "Created workflow with " << workflow.tasks.size() << " tasks\n";
    
    // 验证工作流
    // (需要 WorkflowEngine 实例)
}

// ==================== 示例 3: 指标收集 ====================
void example_metrics_collection() {
    std::cout << "\n=== Example 3: Metrics Collection ===\n";
    
    auto collector = std::make_shared<MetricsCollector>();
    
    // 设置工作流信息
    collector->set_workflow_info("wf_test", "exec_test");
    
    // 模拟任务执行
    collector->record_task_start("task1", "llm");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    collector->record_task_complete("task1", TaskResult::ok("result1"));
    collector->record_tokens(500);
    
    collector->record_task_start("task2", "tool");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    collector->record_task_complete("task2", TaskResult::ok("result2"));
    collector->record_tool_call();
    
    // 设置总时长
    collector->set_total_duration(std::chrono::milliseconds(150));
    
    // 计算成本
    collector->calculate_cost(0.003);  // $0.003 per 1K tokens
    
    // 获取指标
    auto metrics = collector->get_metrics();
    
    std::cout << "Metrics:\n";
    std::cout << "  Total duration: " << metrics.total_duration.count() << "ms\n";
    std::cout << "  LLM time: " << metrics.llm_time.count() << "ms\n";
    std::cout << "  Tool time: " << metrics.tool_time.count() << "ms\n";
    std::cout << "  Total tokens: " << metrics.total_tokens << "\n";
    std::cout << "  Tool calls: " << metrics.total_tool_calls << "\n";
    std::cout << "  Estimated cost: $" << metrics.estimated_cost << "\n";
    std::cout << "  Parallel tasks peak: " << metrics.parallel_tasks_peak << "\n";
    
    // 导出 Prometheus 格式
    std::cout << "\nPrometheus format:\n";
    std::cout << metrics.to_prometheus() << "\n";
}

// ==================== 示例 4: DAG 分析 ====================
void example_dag_analysis() {
    std::cout << "\n=== Example 4: DAG Analysis ===\n";
    
    DAG dag;
    
    // 添加任务
    auto task1 = TaskFactory::create_function_task("A", [](const TaskContext&) {
        return TaskResult::ok("Task A");
    });
    
    auto task2 = TaskFactory::create_function_task("B", [](const TaskContext&) {
        return TaskResult::ok("Task B");
    });
    
    auto task3 = TaskFactory::create_function_task("C", [](const TaskContext&) {
        return TaskResult::ok("Task C");
    });
    
    auto task4 = TaskFactory::create_function_task("D", [](const TaskContext&) {
        return TaskResult::ok("Task D");
    });
    
    dag.add_task("A", task1);
    dag.add_task("B", task2);
    dag.add_task("C", task3);
    dag.add_task("D", task4);
    
    // 添加依赖
    dag.add_dependency("A", "B");  // B depends on A
    dag.add_dependency("A", "C");  // C depends on A
    dag.add_dependency("B", "D");  // D depends on B
    dag.add_dependency("C", "D");  // D depends on C
    
    std::cout << "DAG structure:\n";
    std::cout << "  A → B → D\n";
    std::cout << "  A → C → D\n\n";
    
    // 检查环
    std::cout << "Has cycle: " << (dag.has_cycle() ? "Yes" : "No") << "\n";
    
    // 拓扑排序
    auto sorted = dag.topological_sort();
    std::cout << "Topological order: ";
    for (const auto& id : sorted) {
        std::cout << id << " ";
    }
    std::cout << "\n";
    
    // 获取就绪任务
    std::unordered_set<TaskId> completed;
    auto ready = dag.get_ready_tasks(completed);
    std::cout << "Ready tasks (initial): ";
    for (const auto& id : ready) {
        std::cout << id << " ";
    }
    std::cout << "\n";
    
    // 标记 A 完成
    completed.insert("A");
    ready = dag.get_ready_tasks(completed);
    std::cout << "Ready tasks (after A): ";
    for (const auto& id : ready) {
        std::cout << id << " ";
    }
    std::cout << "\n";
}

// ==================== 示例 5: 工作流验证 ====================
void example_workflow_validation() {
    std::cout << "\n=== Example 5: Workflow Validation ===\n";
    
    // 创建线程池
    auto thread_pool = std::make_shared<ThreadPool>();
    
    // 创建引擎（需要 SharedResources，这里简化）
    // auto engine = std::make_shared<WorkflowEngine>(resources, thread_pool);
    
    // 测试 1: 有效的工作流
    WorkflowDefinition valid_workflow;
    valid_workflow.id = "valid_wf";
    valid_workflow.tasks.push_back({.id = "task1", .type = "function"});
    valid_workflow.tasks.push_back({.id = "task2", .type = "function", .depends_on = {"task1"}});
    
    std::cout << "Valid workflow: task1 → task2\n";
    std::cout << "  Expected: Valid\n";
    
    // 测试 2: 循环依赖
    WorkflowDefinition cyclic_workflow;
    cyclic_workflow.id = "cyclic_wf";
    cyclic_workflow.tasks.push_back({.id = "A", .type = "function", .depends_on = {"C"}});
    cyclic_workflow.tasks.push_back({.id = "B", .type = "function", .depends_on = {"A"}});
    cyclic_workflow.tasks.push_back({.id = "C", .type = "function", .depends_on = {"B"}});
    
    std::cout << "\nCyclic workflow: A → B → C → A\n";
    std::cout << "  Expected: Invalid (cycle detected)\n";
    
    // 测试 3: 重复任务 ID
    WorkflowDefinition duplicate_workflow;
    duplicate_workflow.id = "duplicate_wf";
    duplicate_workflow.tasks.push_back({.id = "task1", .type = "function"});
    duplicate_workflow.tasks.push_back({.id = "task1", .type = "function"});  // 重复
    
    std::cout << "\nDuplicate task ID workflow\n";
    std::cout << "  Expected: Invalid (duplicate ID)\n";
}

// ==================== Main ====================
int main() {
    std::cout << "========================================\n";
    std::cout << "  WorkflowEngine Examples\n";
    std::cout << "========================================\n";
    
    example_builtin_template();
    example_dynamic_workflow();
    example_metrics_collection();
    example_dag_analysis();
    example_workflow_validation();
    
    std::cout << "\n========================================\n";
    std::cout << "  All examples completed!\n";
    std::cout << "========================================\n";
    
    return 0;
}
