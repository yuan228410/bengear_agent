#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <sstream>
#include <chrono>
#include <random>

namespace ben_gear {
namespace workflow {

WorkflowEngine::WorkflowEngine(
    std::shared_ptr<agent::SharedResources> resources,
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool)
    : resources_(std::move(resources))
    , storage_(std::make_shared<MemoryStorage>()) {
    
    // 创建或使用传入的线程池
    if (!thread_pool) {
        thread_pool = std::make_shared<base::concurrency::ThreadPool>();
    }
    executor_ = std::make_shared<TaskExecutor>(thread_pool);
}

void WorkflowEngine::register_workflow(const WorkflowDefinition& workflow) {
    std::unique_lock lock(mutex_);
    workflows_[workflow.id] = workflow;
    
    log::info_fmt("workflow registered: id={}, name={}, tasks={}", 
                  workflow.id, workflow.name, workflow.tasks.size());
}

WorkflowEngine::ValidationResult WorkflowEngine::validate_workflow(const WorkflowDefinition& workflow) {
    // 检查任务 ID 唯一性
    std::set<std::string> task_ids;
    for (const auto& task : workflow.tasks) {
        if (task_ids.count(task.id)) {
            return {false, "Duplicate task ID: " + task.id};
        }
        task_ids.insert(task.id);
    }
    
    // 检查依赖是否存在
    for (const auto& task : workflow.tasks) {
        for (const auto& dep : task.depends_on) {
            if (!task_ids.count(dep)) {
                return {false, "Task '" + task.id + "' depends on non-existent task: " + dep};
            }
        }
    }
    
    // 构建临时 DAG 检查环
    try {
        DAG dag;
        for (const auto& task : workflow.tasks) {
            auto dummy = TaskFactory::create_function_task(task.id, [](const TaskContext&) {
                return TaskResult::ok();
            });
            dag.add_task(task.id, dummy);
        }
        for (const auto& task : workflow.tasks) {
            for (const auto& dep : task.depends_on) {
                dag.add_dependency(dep, task.id);
            }
        }
        
        if (dag.has_cycle()) {
            return {false, "Workflow contains cycle"};
        }
    } catch (const std::exception& e) {
        return {false, std::string("DAG validation failed: ") + e.what()};
    }
    
    return {true, ""};
}

std::string WorkflowEngine::generate_execution_id() {
    // 使用时间戳、随机数和原子计数器生成唯一 ID
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    // 使用更长的随机数（16位十六进制 = 64位随机数）
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t random_num = dis(gen);
    
    std::ostringstream oss;
    oss << "exec_" << millis << "_" << std::hex << random_num;
    return oss.str();
}

DAG WorkflowEngine::build_dag(const WorkflowDefinition& workflow) {
    DAG dag;
    
    for (const auto& task_def : workflow.tasks) {
        auto task = create_task(task_def, workflow);
        dag.add_task(task_def.id, task);
    }
    
    for (const auto& task_def : workflow.tasks) {
        for (const auto& dep : task_def.depends_on) {
            dag.add_dependency(dep, task_def.id);
        }
    }
    
    return dag;
}

TaskPtr WorkflowEngine::create_task(
    const WorkflowTaskDefinition& task_def,
    const WorkflowDefinition& workflow) {
    
    if (task_def.type == "llm") {
        // 创建 LLM 任务
        LLMTaskConfig config;
        config.prompt = task_def.prompt;
        
        if (task_def.config.contains("model")) {
            config.model = task_def.config["model"].get<std::string>();
        }
        if (task_def.config.contains("timeout_seconds")) {
            config.timeout_seconds = task_def.config["timeout_seconds"].get<int>();
        }
        
        // 创建 Agent
        auto agent = std::make_shared<agent::Agent>(resources_);
        
        return TaskFactoryEx::create_llm_task(task_def.id, agent, config);
        
    } else if (task_def.type == "tool") {
        // 创建 Tool 任务
        ToolTaskConfig config;
        
        // 解析工具名称和参数
        if (task_def.config.contains("tool_name")) {
            config.tool_name = task_def.config["tool_name"].get<std::string>();
            config.arguments = task_def.config.value("arguments", Json::object());
        } else {
            // 简单格式解析
            auto pos = task_def.prompt.find(':');
            if (pos != std::string::npos) {
                config.tool_name = task_def.prompt.substr(0, pos);
                config.arguments = Json::object();
            } else {
                config.tool_name = task_def.prompt;
                config.arguments = Json::object();
            }
        }
        
        return TaskFactoryEx::create_tool_task(task_def.id, 
            std::shared_ptr<llm::ToolRegistry>(const_cast<llm::ToolRegistry*>(&resources_->tools()), 
                [](llm::ToolRegistry*){}),  // 空删除器，不拥有所有权
            config);
        
    } else {
        // 默认：函数任务
        return TaskFactory::create_function_task(task_def.id, 
            [prompt = task_def.prompt](const TaskContext&) {
                return TaskResult::ok(prompt);
            });
    }
}

WorkflowState WorkflowEngine::execute(const std::string& workflow_id) {
    // 获取工作流定义
    auto workflow_opt = get_workflow(workflow_id);
    if (!workflow_opt) {
        WorkflowState state;
        state.status = WorkflowStatus::FAILED;
        state.error_message = "Workflow not found: " + workflow_id;
        return state;
    }
    
    auto workflow = *workflow_opt;
    
    // 验证工作流
    auto validation = validate_workflow(workflow);
    if (!validation.valid) {
        WorkflowState state;
        state.status = WorkflowStatus::FAILED;
        state.error_message = validation.error;
        return state;
    }
    
    // 生成执行 ID
    auto execution_id = generate_execution_id();
    
    // 初始化状态
    WorkflowState state;
    state.id = execution_id;
    state.status = WorkflowStatus::RUNNING;
    state.started_at = std::chrono::system_clock::now();
    
    // 构建 DAG
    auto dag = build_dag(workflow);
    
    // 创建调度器
    auto scheduler = std::make_shared<WorkflowScheduler>(dag, executor_, error_strategy_);
    
    // 记录活跃调度器
    {
        std::unique_lock lock(mutex_);
        active_schedulers_[execution_id] = scheduler;
        running_workflows_[execution_id] = state;
    }
    
    // 执行工作流
    auto result = scheduler->run();
    
    // 更新状态
    state.status = result.status;
    state.task_results = result.task_results;
    state.error_message = result.error_message;
    state.completed_at = std::chrono::system_clock::now();
    
    // 清理
    {
        std::unique_lock lock(mutex_);
        active_schedulers_.erase(execution_id);
        running_workflows_[execution_id] = state;
    }
    
    return state;
}

bool WorkflowEngine::pause(const std::string& execution_id) {
    std::shared_lock lock(mutex_);
    auto it = active_schedulers_.find(execution_id);
    if (it != active_schedulers_.end()) {
        it->second->pause();
        log::info_fmt("workflow paused: execution_id={}", execution_id);
        return true;
    }
    return false;
}

bool WorkflowEngine::resume(const std::string& execution_id) {
    std::shared_lock lock(mutex_);
    auto it = active_schedulers_.find(execution_id);
    if (it != active_schedulers_.end()) {
        it->second->resume();
        log::info_fmt("workflow resumed: execution_id={}", execution_id);
        return true;
    }
    return false;
}

bool WorkflowEngine::cancel(const std::string& execution_id) {
    std::shared_lock lock(mutex_);
    auto it = active_schedulers_.find(execution_id);
    if (it != active_schedulers_.end()) {
        it->second->cancel();
        log::info_fmt("workflow cancelled: execution_id={}", execution_id);
        return true;
    }
    return false;
}

std::optional<WorkflowState> WorkflowEngine::get_state(const std::string& execution_id) const {
    std::shared_lock lock(mutex_);
    auto it = running_workflows_.find(execution_id);
    if (it != running_workflows_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<WorkflowDefinition> WorkflowEngine::get_workflow(const std::string& workflow_id) const {
    std::shared_lock lock(mutex_);
    auto it = workflows_.find(workflow_id);
    if (it != workflows_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool WorkflowEngine::add_task(
    const std::string& execution_id,
    const WorkflowTaskDefinition& task,
    const std::string& after_task) {
    
    // TODO: 实现动态添加任务
    log::warn_fmt("add_task not implemented yet");
    return false;
}

} // namespace workflow
} // namespace ben_gear
