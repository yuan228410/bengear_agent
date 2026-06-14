#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <sstream>
#include <set>
#include <chrono>
#include <random>
#include <future>

namespace ben_gear {
namespace workflow {

WorkflowEngine::WorkflowEngine(
    WorkflowResources resources,
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool)
    : resources_(std::move(resources))
    , storage_(std::make_shared<MemoryStorage>())
    , metrics_(std::make_shared<MetricsCollector>()) {

    executor_ = std::make_shared<TaskExecutor>(thread_pool);

    if (resources_.settings) {
        retry_policy_.max_retries = resources_.settings->workflow.max_retries;
        retry_policy_.retry_delay_ms = resources_.settings->workflow.retry_delay_ms;
    }

    if (thread_pool) {
        log::info_fmt("WorkflowEngine: using custom thread pool");
    } else {
        log::info_fmt("WorkflowEngine: using std::async (no thread pool)");
    }
}

std::string WorkflowEngine::register_workflow(const WorkflowDefinition& workflow,
                                              const std::string& ns) {
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
    std::set<std::string> task_ids;
    for (const auto& task : workflow.tasks) {
        if (task_ids.count(task.id)) {
            return {false, "Duplicate task ID: " + task.id};
        }
        task_ids.insert(task.id);
    }

    for (const auto& task : workflow.tasks) {
        if (task.type != "llm" && task.type != "tool" && task.type != "function" &&
            task.type != "condition" && task.type != "subflow" &&
            task.type != "approval" && task.type != "sub_agent") {
            return {false, "Unsupported task type: " + task.type};
        }
        for (const auto& dep : task.depends_on) {
            if (!task_ids.count(dep)) {
                return {false, "Task '" + task.id + "' depends on non-existent task: " + dep};
            }
        }
    }

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
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

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
        LLMTaskConfig config;
        config.prompt = task_def.prompt;

        if (task_def.config.contains("model")) {
            config.model = task_def.config["model"].get<std::string>();
        }
        if (task_def.config.contains("timeout_seconds")) {
            config.timeout_seconds = task_def.config["timeout_seconds"].get<int>();
        }
        if (config.timeout_seconds <= 0 && resources_.settings) {
            config.timeout_seconds = resources_.settings->workflow.task_timeout;
        }

        if (!resources_.is_bound()) {
            log::error_fmt("workflow: cannot create LLM task '{}', resources not bound", task_def.id);
            return TaskFactory::create_function_task(task_def.id,
                [id = task_def.id](const TaskContext&) {
                    return TaskResult::error("LLM task failed: resources not bound");
                });
        }
        if (!resources_.run_chat_async) {
            log::error_fmt("workflow: cannot create LLM task '{}', run_chat_async not bound", task_def.id);
            return TaskFactory::create_function_task(task_def.id,
                [id = task_def.id](const TaskContext&) {
                    return TaskResult::error("LLM task failed: run_chat_async not bound");
                });
        }

        return TaskFactoryEx::create_llm_task(task_def.id, resources_, config);

    } else if (task_def.type == "tool") {
        if (!resources_.is_bound()) {
            log::error_fmt("workflow: cannot create Tool task '{}', resources not bound", task_def.id);
            return TaskFactory::create_function_task(task_def.id,
                [id = task_def.id](const TaskContext&) {
                    return TaskResult::error("Tool task failed: resources not bound");
                });
        }

        std::string func_name = task_def.config.value("function", "");

        if (task_def.config.contains("tool_name")) {
            func_name = task_def.config["tool_name"].get<std::string>();
        }
        if (task_def.config.contains("tool")) {
            func_name = task_def.config["tool"].get<std::string>();
        }

        if (!func_name.empty() && resources_.tools) {
            ToolTaskConfig config;
            config.tool_name = func_name;
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
                std::shared_ptr<llm::ToolRegistry>(resources_.lifetime_context, resources_.tools),
                config);
        }

        return TaskFactory::create_function_task(task_def.id,
            [prompt = task_def.prompt](const TaskContext&) {
                return TaskResult::ok(prompt);
            });
    } else if (task_def.type == "function") {
        return TaskFactory::create_function_task(task_def.id,
            [prompt = task_def.prompt](const TaskContext&) {
                return TaskResult::ok(base::container::String(prompt));
            });
    } else if (task_def.type == "condition") {
        ConditionTaskConfig config;
        config.expression = task_def.config.value("expression", task_def.prompt);
        config.default_value = task_def.config.value("default", false);
        return TaskFactoryEx::create_condition_task(task_def.id, config);
    } else if (task_def.type == "subflow") {
        SubflowTaskConfig config;
        config.workflow_id = task_def.config.value("workflow_id", task_def.prompt);
        return TaskFactoryEx::create_subflow_task(task_def.id, resources_, config);
    } else if (task_def.type == "approval") {
        HumanApprovalConfig config;
        config.message = task_def.config.value("message", task_def.prompt);
        config.context = task_def.config.value("context", Json::object());
        config.timeout = std::chrono::seconds(task_def.config.value("timeout_seconds", 3600));
        return TaskFactoryEx::create_approval_task(task_def.id, config);
    } else if (task_def.type == "sub_agent") {
        if (!resources_.is_bound()) {
            return TaskFactory::create_function_task(task_def.id,
                [](const TaskContext&) {
                    return TaskResult::error("Sub-agent task failed: resources not bound");
                });
        }
        SubAgentTaskConfig config;
        config.prompt = task_def.config.value("prompt", task_def.prompt);
        config.model_override = task_def.config.value("model_override", task_def.config.value("model", ""));
        config.arguments = task_def.config.value("arguments", Json::object());
        return TaskFactoryEx::create_sub_agent_task(task_def.id,
            std::shared_ptr<llm::ToolRegistry>(resources_.lifetime_context, resources_.tools),
            config);
    }

    return TaskFactory::create_function_task(task_def.id,
        [type = task_def.type](const TaskContext&) {
            return TaskResult::error("Unsupported task type: " + type);
        });
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

    // 重置 metrics 并设置工作流信息
    if (metrics_) {
        metrics_->reset();
        metrics_->set_workflow_info(workflow_id, execution_id);
    }

    // 通知工作流开始
    if (progress_callbacks_) {
        progress_callbacks_->on_workflow_started(workflow_id, execution_id, static_cast<int>(workflow.tasks.size()));
    }

    WorkflowState state;
    state.id = execution_id;
    state.status = WorkflowStatus::RUNNING;
    state.started_at = std::chrono::system_clock::now();

    auto dag = build_dag(workflow);
    // 将 progress_callbacks 和 metrics 传递给 Scheduler
    auto scheduler = std::make_shared<WorkflowScheduler>(
        dag, executor_, error_strategy_, retry_policy_,
        progress_callbacks_, metrics_, workflow_id, execution_id);

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

    // 通知工作流完成
    if (progress_callbacks_) {
        progress_callbacks_->on_workflow_completed(workflow_id, execution_id, state);
    }

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


std::string WorkflowEngine::start_async(const std::string& workflow_id) {
    auto workflow_opt = get_workflow(workflow_id);
    if (!workflow_opt) {
        log::error_fmt("workflow start_async: not found, id={}", workflow_id);
        return {};
    }

    const auto& workflow = *workflow_opt;
    auto validation = validate_workflow(workflow);
    if (!validation.valid) {
        log::error_fmt("workflow start_async: validation failed, id={}, error={}", workflow_id, validation.error);
        return {};
    }

    auto execution_id = generate_execution_id();
    log::info_fmt("workflow start_async: start, id={}, name={}, tasks={}, execution_id={}",
                  workflow_id, workflow.name, workflow.tasks.size(), execution_id);

    if (metrics_) {
        metrics_->reset();
        metrics_->set_workflow_info(workflow_id, execution_id);
    }

    if (progress_callbacks_) {
        progress_callbacks_->on_workflow_started(workflow_id, execution_id, static_cast<int>(workflow.tasks.size()));
    }

    auto dag = build_dag(workflow);
    auto scheduler = std::make_shared<WorkflowScheduler>(
        dag, executor_, error_strategy_, retry_policy_, progress_callbacks_, metrics_, workflow_id, execution_id);

    WorkflowState state;
    state.id = execution_id;
    state.status = WorkflowStatus::RUNNING;
    state.started_at = std::chrono::system_clock::now();

    {
        std::unique_lock lock(mutex_);
        active_schedulers_[execution_id] = scheduler;
        running_workflows_[execution_id] = state;
    }

    auto callbacks = progress_callbacks_;
    auto future = std::async(std::launch::async,
        [scheduler, execution_id, workflow_id, callbacks, this]() mutable -> WorkflowResult {
            WorkflowResult result;
            try {
                result = scheduler->run();
            } catch (const std::exception& e) {
                result.status = WorkflowStatus::FAILED;
                result.success = false;
                result.error_message = e.what();
                log::error_fmt("workflow start_async: scheduler exception, execution_id={}, error={}",
                               execution_id, e.what());
            }

            WorkflowState final_state;
            final_state.id = execution_id;
            final_state.status = result.status;
            final_state.error_message = result.error_message;
            final_state.task_results = result.task_results;
            final_state.completed_at = std::chrono::system_clock::now();

            if (callbacks) {
                callbacks->on_workflow_completed(workflow_id, execution_id, final_state);
            }

            {
                std::unique_lock lock(mutex_);
                active_schedulers_.erase(execution_id);
                running_workflows_[execution_id] = final_state;
            }

            log::info_fmt("workflow start_async: completed, id={}, execution_id={}, status={}",
                          workflow_id, execution_id, static_cast<int>(result.status));
            return result;
        });

    {
        std::unique_lock lock(mutex_);
        active_futures_[execution_id] = std::move(future);
    }

    return execution_id;
}

std::future<WorkflowResult> WorkflowEngine::execute_async(const std::string& workflow_id) {
    auto workflow_opt = get_workflow(workflow_id);
    if (!workflow_opt) {
        log::error_fmt("workflow execute_async: not found, id={}", workflow_id);
        std::promise<WorkflowResult> prom;
        WorkflowResult result;
        result.status = WorkflowStatus::FAILED;
        result.error_message = "Workflow not found: " + workflow_id;
        prom.set_value(std::move(result));
        return prom.get_future();
    }

    auto workflow = *workflow_opt;
    auto validation = validate_workflow(workflow);
    if (!validation.valid) {
        log::error_fmt("workflow execute_async: validation failed, id={}, error={}", workflow_id, validation.error);
        std::promise<WorkflowResult> prom;
        WorkflowResult result;
        result.status = WorkflowStatus::FAILED;
        result.error_message = validation.error;
        prom.set_value(std::move(result));
        return prom.get_future();
    }

    auto execution_id = generate_execution_id();
    log::info_fmt("workflow execute_async: start, id={}, name={}, tasks={}, execution_id={}",
                  workflow_id, workflow.name, workflow.tasks.size(), execution_id);

    if (metrics_) {
        metrics_->reset();
        metrics_->set_workflow_info(workflow_id, execution_id);
    }

    // 在调用线程触发 on_workflow_started，保证时序
    if (progress_callbacks_) {
        progress_callbacks_->on_workflow_started(workflow_id, execution_id, static_cast<int>(workflow.tasks.size()));
    }

    auto dag = build_dag(workflow);
    auto scheduler = std::make_shared<WorkflowScheduler>(
        dag, executor_, error_strategy_, retry_policy_,
        progress_callbacks_, metrics_, workflow_id, execution_id);

    // 初始化运行状态
    WorkflowState state;
    state.id = execution_id;
    state.status = WorkflowStatus::RUNNING;
    state.started_at = std::chrono::system_clock::now();

    {
        std::unique_lock lock(mutex_);
        active_schedulers_[execution_id] = scheduler;
        running_workflows_[execution_id] = state;
    }

    // scheduler->run_async() 在独立线程执行
    // 包装 future：完成后触发 on_workflow_completed + 清理 active_schedulers_
    // 使用 std::async 而非额外线程，复用已有线程资源
    auto callbacks = progress_callbacks_;
    auto raw_future = scheduler->run_async();

    return std::async(std::launch::async,
        [raw_future = std::move(raw_future), execution_id, workflow_id, callbacks, this]() mutable -> WorkflowResult {
            auto result = raw_future.get();

            // 更新运行状态
            WorkflowState final_state;
            final_state.id = execution_id;
            final_state.status = result.status;
            final_state.error_message = result.error_message;
            final_state.task_results = result.task_results;
            final_state.completed_at = std::chrono::system_clock::now();

            // 触发 on_workflow_completed
            if (callbacks) {
                callbacks->on_workflow_completed(workflow_id, execution_id, final_state);
            }

            // 清理 active_schedulers_
            {
                std::unique_lock lock(mutex_);
                active_schedulers_.erase(execution_id);
                running_workflows_[execution_id] = final_state;
            }

            log::info_fmt("workflow execute_async: completed, id={}, execution_id={}, status={}",
                          workflow_id, execution_id, static_cast<int>(result.status));

            return result;
        });
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

std::optional<WorkflowState> WorkflowEngine::get_state(const std::string& execution_id) {
    {
        std::unique_lock lock(mutex_);
        auto future_it = active_futures_.find(execution_id);
        if (future_it != active_futures_.end() &&
            future_it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                (void)future_it->second.get();
            } catch (const std::exception& e) {
                log::error_fmt("workflow get_state: async future failed, execution_id={}, error={}",
                               execution_id, e.what());
            }
            active_futures_.erase(future_it);
        }
    }

    std::shared_lock lock(mutex_);
    auto it = running_workflows_.find(execution_id);
    if (it != running_workflows_.end()) return it->second;
    return std::nullopt;
}

std::optional<WorkflowDefinition> WorkflowEngine::get_workflow(const std::string& workflow_id) const {
    std::shared_lock lock(mutex_);
    auto it = workflows_.find(workflow_id);
    if (it != workflows_.end()) return it->second;
    return std::nullopt;
}

std::vector<std::string> WorkflowEngine::list_workflows(const std::string& ns) const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    std::string prefix = ns.empty() ? ns : (ns + "::");
    for (const auto& [id, wf] : workflows_) {
        if (prefix.empty() || id.starts_with(prefix)) result.push_back(id);
    }
    return result;
}

bool WorkflowEngine::add_task(
    const std::string& workflow_id,
    const WorkflowTaskDefinition& task,
    const std::string& after_task) {

    std::unique_lock lock(mutex_);

    // 查找工作流定义
    auto it = workflows_.find(workflow_id);
    if (it == workflows_.end()) {
        log::error_fmt("add_task: workflow not found, id={}", workflow_id);
        return false;
    }

    // 检查是否正在运行
    for (const auto& [exec_id, scheduler] : active_schedulers_) {
        if (scheduler && scheduler->is_running()) {
            log::error_fmt("add_task: cannot modify running workflow, id={}", workflow_id);
            return false;
        }
    }

    auto& workflow = it->second;

    // 检查任务 ID 是否重复
    for (const auto& t : workflow.tasks) {
        if (t.id == task.id) {
            log::error_fmt("add_task: duplicate task id, task_id={}", task.id);
            return false;
        }
    }

    // 验证依赖是否存在
    for (const auto& dep : task.depends_on) {
        bool found = false;
        for (const auto& t : workflow.tasks) {
            if (t.id == dep) { found = true; break; }
        }
        if (!found) {
            log::error_fmt("add_task: dependency not found, task_id={}, dep={}", task.id, dep);
            return false;
        }
    }

    // 如果指定了 after_task，自动添加依赖
    WorkflowTaskDefinition new_task = task;
    if (!after_task.empty()) {
        // 检查 after_task 是否存在
        bool after_found = false;
        for (const auto& t : workflow.tasks) {
            if (t.id == after_task) { after_found = true; break; }
        }
        if (!after_found) {
            log::error_fmt("add_task: after_task not found, after={}", after_task);
            return false;
        }
        // 添加依赖（避免重复）
        bool already_dep = false;
        for (const auto& d : new_task.depends_on) {
            if (d == after_task) { already_dep = true; break; }
        }
        if (!already_dep) {
            new_task.depends_on.push_back(after_task);
        }
    }

    // 添加到工作流定义
    workflow.tasks.push_back(new_task);

    log::info_fmt("add_task: added to workflow, workflow_id={}, task_id={}, depends_on_count={}",
                  workflow_id, new_task.id, new_task.depends_on.size());

    return true;
}

} // namespace workflow
} // namespace ben_gear
