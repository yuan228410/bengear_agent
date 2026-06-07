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
    
    // 线程池策略：
    // - 传入线程池：使用传入的（适合独立配置 I/O 线程池）
    // - 传 nullptr：TaskExecutor 内部使用 std::async（推荐，避免占用核心线程池）
    // 
    // 注意：不传线程池时，TaskExecutor 会降级为 std::async，适合 I/O 密集型任务
    // 如果需要控制并发度，可以传入独立配置的线程池：
    //   auto io_pool = std::make_shared<ThreadPool>(ThreadPoolConfig{.min_threads=2, .max_threads=4});
    //   auto engine = std::make_shared<WorkflowEngine>(nullptr, io_pool);
    
    executor_ = std::make_shared<TaskExecutor>(thread_pool);

    // 从 Settings 填充重试策略默认值
    if (resources_) {
        retry_policy_.max_retries = resources_->settings().workflow.max_retries;
        retry_policy_.retry_delay_ms = resources_->settings().workflow.retry_delay_ms;
    }

    if (thread_pool) {
        log::info_fmt("WorkflowEngine: using custom thread pool");
    } else {
        log::info_fmt("WorkflowEngine: using std::async (no thread pool)");
    }
}

std::string WorkflowEngine::register_workflow(const WorkflowDefinition& workflow,
                                              const std::string& ns) {
 // 自动使用 thread_local 命名空间（工具调用时由 Agent 设置）
 const auto& effective_ns = ns.empty() ? current_namespace() : ns;
 std::unique_lock lock(mutex_);
 std::string namespaced_id = effective_ns.empty() ? workflow.id : (effective_ns + "::" + workflow.id);
 WorkflowDefinition namespaced_workflow = workflow;
 namespaced_workflow.id = namespaced_id;
 workflows_[namespaced_id] = namespaced_workflow;

 log::info_fmt("workflow registered: id={}, name={}, tasks={}, ns={}",
 namespaced_id, workflow.name, workflow.tasks.size(), effective_ns);

 return namespaced_id;
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
    const WorkflowDefinition& /*workflow*/) {
    
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
        // 未指定超时时使用 Settings 中的默认值
        if (config.timeout_seconds <= 0 && resources_) {
            config.timeout_seconds = resources_->settings().workflow.task_timeout;
        }
        
        // 创建 Agent（需要 resources_ 已绑定）
        if (!resources_) {
            log::error_fmt("workflow: cannot create LLM task '{}', resources not bound", task_def.id);
            return TaskFactory::create_function_task(task_def.id,
                [id = task_def.id](const TaskContext&) {
                    return TaskResult::error("LLM task failed: resources not bound");
                });
        }
        auto agent = std::make_shared<agent::Agent>(resources_);
        
        return TaskFactoryEx::create_llm_task(task_def.id, agent, config);
        
    } else if (task_def.type == "tool") {
        // 创建 Tool 任务（需要 resources_ 已绑定）
        if (!resources_) {
            log::error_fmt("workflow: cannot create tool task '{}', resources not bound", task_def.id);
            return TaskFactory::create_function_task(task_def.id,
                [id = task_def.id](const TaskContext&) {
                    return TaskResult::error("tool task failed: resources not bound");
                });
        }
        ToolTaskConfig config;
        
        // 解析工具名称
        if (task_def.config.contains("tool_name")) {
            config.tool_name = task_def.config["tool_name"].get<std::string>();
        } else if (task_def.config.contains("tool")) {
            config.tool_name = task_def.config["tool"].get<std::string>();
        } else {
            auto pos = task_def.prompt.find(':');
            config.tool_name = (pos != std::string::npos) ? task_def.prompt.substr(0, pos) : task_def.prompt;
        }
        
        // 解析工具参数：兼容 arguments/params/parameters/tool_input/tool_params
        if (task_def.config.contains("arguments")) {
            config.arguments = task_def.config["arguments"];
        } else if (task_def.config.contains("tool_params")) {
            config.arguments = task_def.config["tool_params"];
        } else if (task_def.config.contains("params")) {
            config.arguments = task_def.config["params"];
        } else if (task_def.config.contains("parameters")) {
            config.arguments = task_def.config["parameters"];
        } else if (task_def.config.contains("tool_input")) {
            config.arguments = task_def.config["tool_input"];
        } else {
            config.arguments = Json::object();
        }
        
        // 未指定超时时使用 Settings 中的默认值
        if (config.timeout_seconds <= 0 && resources_) {
            config.timeout_seconds = resources_->settings().workflow.task_timeout;
        }
        log::debug_fmt("workflow: tool task parsed, id={}, tool_name={}, args_keys={}",
            task_def.id, config.tool_name,
            config.arguments.is_object() ? std::to_string(config.arguments.size()) + " keys" : "empty");
        if (config.arguments.is_object()) {
            std::string keys;
            for (auto it = config.arguments.begin(); it != config.arguments.end(); ++it) {
                if (!keys.empty()) keys += ",";
                keys += it.key();
            }
            log::debug_fmt("workflow: tool task arguments keys: [{}]", keys);
        }

        // 安全方案：使用非拥有指针包装器，避免空删除器悬空风险
        // ToolRegistry 生命周期由 SharedResources 管理，必须确保 resources_ 有效
        return TaskFactoryEx::create_tool_task(task_def.id,
            std::shared_ptr<llm::ToolRegistry>(resources_, &const_cast<llm::ToolRegistry&>(resources_->tools())),
            config);
        
    } else {
        // 默认：函数任务
        // 支持 config.function/config.tool 指定要调用的工具
        std::string func_name;
        if (task_def.config.contains("function")) {
            func_name = task_def.config["function"].get<std::string>();
        } else if (task_def.config.contains("tool")) {
            func_name = task_def.config["tool"].get<std::string>();
        }
        
        if (!func_name.empty() && resources_) {
            // 有指定工具名且有 resources，转为 tool 任务执行
            ToolTaskConfig config;
            config.tool_name = func_name;
            // 兼容 params/parameters/arguments/tool_params
            if (task_def.config.contains("tool_params")) {
                config.arguments = task_def.config["tool_params"];
            } else if (task_def.config.contains("params")) {
                config.arguments = task_def.config["params"];
            } else if (task_def.config.contains("parameters")) {
                config.arguments = task_def.config["parameters"];
            } else if (task_def.config.contains("arguments")) {
                config.arguments = task_def.config["arguments"];
            } else {
                config.arguments = Json::object();
            }
            return TaskFactoryEx::create_tool_task(task_def.id,
                std::shared_ptr<llm::ToolRegistry>(const_cast<llm::ToolRegistry*>(&resources_->tools()),
                    [](llm::ToolRegistry*){}),
                config);
        }
        
        // 没有指定工具，返回 prompt 文本
        return TaskFactory::create_function_task(task_def.id, 
            [prompt = task_def.prompt](const TaskContext&) {
                return TaskResult::ok(prompt);
            });
    }
}

WorkflowState WorkflowEngine::execute(const std::string& workflow_id) {
    auto workflow_opt = get_workflow(workflow_id);
    if (!workflow_opt) {
        log::error_fmt("workflow execute: not found, id={}", workflow_id);
        WorkflowState state;
        state.status = WorkflowStatus::FAILED;
        state.error_message = "Workflow not found: " + workflow_id;
        return state;
    }

    auto workflow = *workflow_opt;
    auto validation = validate_workflow(workflow);
    if (!validation.valid) {
        log::error_fmt("workflow execute: validation failed, id={}, error={}", workflow_id, validation.error);
        WorkflowState state;
        state.status = WorkflowStatus::FAILED;
        state.error_message = validation.error;
        return state;
    }

    auto execution_id = generate_execution_id();
    log::info_fmt("workflow execute: start, id={}, name={}, tasks={}, execution_id={}",
                  workflow_id, workflow.name, workflow.tasks.size(), execution_id);

    WorkflowState state;
    state.id = execution_id;
    state.status = WorkflowStatus::RUNNING;
    state.started_at = std::chrono::system_clock::now();

    auto dag = build_dag(workflow);
    auto scheduler = std::make_shared<WorkflowScheduler>(dag, executor_, error_strategy_);

    {
        std::unique_lock lock(mutex_);
        active_schedulers_[execution_id] = scheduler;
        running_workflows_[execution_id] = state;
    }

    auto result = scheduler->run();

    state.status = result.status;
    state.task_results = result.task_results;
    state.error_message = result.error_message;
    state.completed_at = std::chrono::system_clock::now();

    auto success_count = std::count_if(result.task_results.begin(), result.task_results.end(),
                                       [](const auto& p) { return p.second.success; });
    log::info_fmt("workflow execute: done, id={}, execution_id={}, status={}, tasks={}/{}/{}",
                  workflow_id, execution_id, static_cast<int>(result.status),
                  success_count, result.task_results.size() - success_count, result.task_results.size());

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

std::vector<std::string> WorkflowEngine::list_workflows(const std::string& ns) const {
 std::shared_lock lock(mutex_);
 std::vector<std::string> result;
 std::string prefix = ns.empty() ? ns : (ns + "::");
 for (const auto& [id, wf] : workflows_) {
     if (prefix.empty() || id.starts_with(prefix)) {
         result.push_back(id);
     }
 }
 return result;
}

bool WorkflowEngine::add_task(
    const std::string& /*execution_id*/,
    const WorkflowTaskDefinition& /*task*/,
    const std::string& /*after_task*/) {
    
    // TODO: 实现动态添加任务
    log::warn_fmt("add_task not implemented yet");
    return false;
}

} // namespace workflow
} // namespace ben_gear
