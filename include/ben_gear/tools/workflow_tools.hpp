#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/workflow/workflow_templates.hpp"
#include "ben_gear/workflow/metrics.hpp"
#include "ben_gear/workflow/human_approval.hpp"
#include "ben_gear/workflow/visualizer.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::llm;
using namespace ben_gear::workflow;

// 前向声明（实现在下方）
inline void register_workflow_tools_with_resources(
    ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine,
    std::shared_ptr<WorkflowTemplateLibrary> templates);

/// 注册工作流工具（需要引擎和模板库，由 SharedResources::post_init 调用）
inline void register_workflow_tools(ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine = nullptr,
    std::shared_ptr<WorkflowTemplateLibrary> templates = nullptr) {
    if (engine && templates) {
        register_workflow_tools_with_resources(registry, engine, templates);
    }
}

/// 注册工作流工具
/// 注册工作流工具（直接传入引擎和模板库，避免循环依赖）
inline void register_workflow_tools_with_resources(
    ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine,
    std::shared_ptr<WorkflowTemplateLibrary> templates) {
    
    auto metrics = std::make_shared<MetricsCollector>();
    auto approval = std::make_shared<ApprovalManager>();
    // 1. create_workflow - 创建工作流
    registry.register_tool(
        ben_gear::base::container::String("create_workflow"),
        ben_gear::base::container::String("Create a workflow with multiple tasks. Tasks run in parallel or sequentially based on dependencies. Supported task types: llm, tool, function, condition, subflow, approval, sub_agent. Unknown task types are rejected. Tool tasks use config.tool or config.tool_name as the executable tool name."),
        {
            {ben_gear::base::container::String("name"), ToolParameterSchema{
                .type = ben_gear::base::container::String("string"),
                .description = ben_gear::base::container::String("Workflow name")
            }},
            {ben_gear::base::container::String("tasks"), ToolParameterSchema{
                .type = ben_gear::base::container::String("array"),
                .description = ben_gear::base::container::String("List of tasks. Each task has: id, type (llm/tool/function/condition/subflow/approval/sub_agent), prompt, depends_on (optional), config (optional)")
            }},
            {ben_gear::base::container::String("variables"), ToolParameterSchema{
                .type = ben_gear::base::container::String("object"),
                .description = ben_gear::base::container::String("Global variables for the workflow")
            }},
            {ben_gear::base::container::String("on_failure"), ToolParameterSchema{
                .type = ben_gear::base::container::String("string"),
                .description = ben_gear::base::container::String("Failure handling strategy: abort (default), continue, or rollback")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto name = args.value("name", "unnamed_workflow");
                auto tasks_json = args.value("tasks", Json::array());
                auto variables = args.value("variables", Json::object());
                auto on_failure = args.value("on_failure", "abort");
                
                WorkflowDefinition workflow;
                workflow.id = "wf_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                workflow.name = name;
                workflow.variables = variables;
                workflow.on_failure = on_failure;
                
                for (const auto& task_json : tasks_json) {
                    WorkflowTaskDefinition task;
                    task.id = task_json.value("id", "");
                    task.name = task_json.value("name", task.id);
                    task.type = task_json.value("type", "function");
                    task.prompt = task_json.value("prompt", "");
                    
                    if (task_json.contains("depends_on") && task_json["depends_on"].is_array()) {
                        for (const auto& dep : task_json["depends_on"]) {
                            task.depends_on.push_back(dep.get<std::string>());
                        }
                    }
                    
                    if (task_json.contains("config")) {
                        task.config = task_json["config"];
                    }
                    
                    // 验证任务类型：全部支持的类型都有真实 ITask 实现，禁止未知类型静默降级。
                    if (task.type != "llm" && task.type != "tool" && task.type != "function" &&
                        task.type != "condition" && task.type != "subflow" &&
                        task.type != "approval" && task.type != "sub_agent") {
                        Json error_result;
                        error_result["success"] = false;
                        error_result["code"] = "unsupported_task_type";
                        error_result["error"] = "Unsupported task type: " + task.type +
                            ". Supported types: llm, tool, function, condition, subflow, approval, sub_agent";
                        return container::String(error_result.dump().c_str());
                    }
                    
                    // 校验 tool/function 类型任务的工具名是否存在
                    if ((task.type == "tool" || task.type == "function") && task.config.contains("tool")) {
                        std::string tool_name = task.config["tool"].get<std::string>();
                        // 检查工具名是否包含空格（常见错误：传了描述而非名称）
                        if (tool_name.find(' ') != std::string::npos) {
                            Json error_result;
                            error_result["success"] = false;
                            error_result["error"] = std::string("Invalid tool name [") + tool_name + "] (contains spaces). Use exact tool name like: http_get, execute_command, read_file, etc.";
                            return container::String(error_result.dump().c_str());
                        }
                    }
                    
                    workflow.tasks.push_back(task);
                }
                
                // 验证工作流
                auto validation = engine->validate_workflow(workflow);
                if (!validation.valid) {
                    Json error_result;
                    error_result["success"] = false;
                    error_result["error"] = validation.error;
                    return container::String(error_result.dump().c_str());
                }
                
                // 注册工作流（自动加命名空间前缀）
                auto namespaced_id = engine->register_workflow(workflow);
                
                Json result;
                result["success"] = true;
                result["workflow_id"] = namespaced_id;
                result["task_count"] = workflow.tasks.size();
                result["message"] = "Workflow created successfully. Use execute_workflow to run it.";
                
                log::info_fmt("workflow created: id={}, name={}, tasks={}", 
                              workflow.id, workflow.name, workflow.tasks.size());
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("create_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 2. execute_workflow - 执行工作流
    registry.register_tool(
        base::container::String("execute_workflow"),
        base::container::String("Execute a created workflow. Returns execution ID for status tracking. Set async=true for background execution, async=false to wait for completion."),
        {
            {base::container::String("workflow_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Workflow ID returned by create_workflow")
            }},
            {base::container::String("async"), ToolParameterSchema{
                .type = base::container::String("boolean"),
                .description = base::container::String("If true, returns immediately with execution_id. If false, waits for completion (default: true)")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto workflow_id = args.value("workflow_id", "");
                auto async = args.value("async", true);
                
                if (workflow_id.empty()) {
                    return container::String(R"({"success": false, "error": "workflow_id is required"})");
                }
                
                if (async) {
                    auto execution_id = engine->start_async(workflow_id);
                    Json result;
                    result["success"] = !execution_id.empty();
                    result["workflow_id"] = workflow_id;
                    result["execution_id"] = execution_id;
                    result["status"] = execution_id.empty() ? static_cast<int>(WorkflowStatus::FAILED) : static_cast<int>(WorkflowStatus::RUNNING);
                    result["status_text"] = execution_id.empty() ? "failed" : "running";
                    result["async"] = true;
                    if (execution_id.empty()) {
                        result["error"] = "Failed to start workflow asynchronously";
                    }
                    return container::String(result.dump().c_str());
                }

                // 同步执行工作流
                auto state = engine->execute(workflow_id);

                Json result;
                result["success"] = (state.status == WorkflowStatus::SUCCESS);
                result["execution_id"] = state.id;
                result["status"] = static_cast<int>(state.status);
                result["status_text"] = workflow_status_name(state.status);
                result["completed_tasks"] = state.task_results.size();
                result["async"] = false;
                
                if (!state.error_message.empty()) {
                    result["error"] = state.error_message;
                }
                
                // 返回每个任务的输出，主 Agent 可直接使用
                Json task_outputs = Json::object();
                for (const auto& [task_id, task_result] : state.task_results) {
                    Json task_info;
                    task_info["success"] = task_result.success;
                    if (!task_result.error_message.empty()) {
                        task_info["error"] = task_result.error_message;
                    }
                    // 提取任务输出（ToolTask/LLMTask 统一输出 container::String）
                    try {
                        if (task_result.output.has_value()) {
                            const auto& val = std::any_cast<const base::container::String&>(task_result.output);
                            auto sv = std::string_view(val.data(), val.size());
                            try {
                                task_info["output"] = Json::parse(sv);
                            } catch (...) {
                                task_info["output"] = std::string(sv);
                            }
                        }
                    } catch (const std::bad_any_cast&) {
                        task_info["output"] = "[non-string result]";
                        log::error_fmt("workflow task output type mismatch: task_id={}, type_name={}", task_id, task_result.output.type().name());
                    }
                    task_outputs[task_id] = task_info;
                }
                result["tasks"] = task_outputs;

                log::info_fmt("workflow executed: workflow_id={}, execution_id={}, status={}, async={}",
                              workflow_id, state.id, static_cast<int>(state.status), async);

                return container::String(result.dump().c_str());

            } catch (const std::exception& e) {
                log::error_fmt("execute_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
            return container::String("Error: unexpected path");
        }
    );

    // 3. get_workflow_status - 查询状态
    registry.register_tool(
        base::container::String("get_workflow_status"),
        base::container::String("Get the current status of a workflow execution, including task progress and results."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID returned by execute_workflow")
            }},
            {base::container::String("include_results"), ToolParameterSchema{
                .type = base::container::String("boolean"),
                .description = base::container::String("Include task results in response (default: false)")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                auto include_results = args.value("include_results", false);
                
                auto state = engine->get_state(execution_id);
                if (!state) {
                    return container::String(R"({"success": false, "error": "Execution not found"})");
                }
                
                Json result;
                result["success"] = true;
                result["execution_id"] = execution_id;
                result["status"] = static_cast<int>(state->status);
                result["task_count"] = state->task_results.size();
                
                // 计算进度
                int completed = 0;
                int failed = 0;
                for (const auto& [task_id, task_result] : state->task_results) {
                    if (task_result.success) completed++;
                    else failed++;
                }
                
                result["progress"]["completed"] = completed;
                result["progress"]["failed"] = failed;
                result["progress"]["total"] = static_cast<uint64_t>(state->task_results.size());
                
                if (state->task_results.size() > 0) {
                    result["progress"]["percentage"] = 
                        (static_cast<uint64_t>(completed) * 100) / static_cast<uint64_t>(state->task_results.size());
                }
                
                // 任务状态列表
                Json tasks = Json::array();
                for (const auto& [task_id, task_result] : state->task_results) {
                    Json task;
                    task["id"] = task_id;
                    task["success"] = task_result.success;
                    
                    if (!task_result.error_message.empty()) {
                        task["error"] = task_result.error_message;
                    }
                    
                    // 包含任务结果（如果请求）
                    if (include_results && task_result.success) {
                        // 简化输出，避免过大
                        task["has_output"] = true;
                    }
                    
                    tasks.push_back(task);
                }
                result["tasks"] = tasks;
                
                // 错误信息
                if (!state->error_message.empty()) {
                    result["error_message"] = state->error_message;
                }
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("get_workflow_status: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 4. list_workflow_templates - 列出模板
    registry.register_tool(
        base::container::String("list_workflow_templates"),
        base::container::String("List available workflow templates."),
        {},
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            (void)args;  // 避免未使用参数告警
            try {
                auto tmpl_list = templates->list();
                
                Json result;
                result["success"] = true;
                result["templates"] = Json::array();
                
                for (const auto& name : tmpl_list) {
                    auto tmpl = templates->get(name);
                    if (tmpl) {
                        Json t;
                        t["id"] = tmpl->id;
                        t["name"] = tmpl->name;
                        t["task_count"] = tmpl->tasks.size();
                        result["templates"].push_back(t);
                    }
                }
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("list_workflow_templates: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 5. load_workflow_template - 加载模板
    registry.register_tool(
        base::container::String("load_workflow_template"),
        base::container::String("Load and customize a workflow template with your variables."),
        {
            {base::container::String("template_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Template ID from list_workflow_templates")
            }},
            {base::container::String("variables"), ToolParameterSchema{
                .type = base::container::String("object"),
                .description = base::container::String("Variables to substitute in the template")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto template_id = args.value("template_id", "");
                auto variables = args.value("variables", Json::object());
                
                auto workflow = templates->instantiate(template_id, variables);
                
                // 注册工作流（自动加命名空间前缀）
                auto namespaced_id = engine->register_workflow(workflow);
                
                Json result;
                result["success"] = true;
                result["workflow_id"] = namespaced_id;
                result["template"] = template_id;
                result["task_count"] = workflow.tasks.size();
                result["message"] = "Template loaded and customized. Use execute_workflow to run it.";
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("load_workflow_template: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 6. pause_workflow - 暂停工作流
    registry.register_tool(
        base::container::String("pause_workflow"),
        base::container::String("Pause a running workflow."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID of the running workflow")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                bool success = engine->pause(execution_id);
                
                Json result;
                result["success"] = success;
                result["execution_id"] = execution_id;
                result["message"] = success ? "Workflow paused successfully" : "Failed to pause workflow";
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("pause_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 7. resume_workflow - 恢复工作流
    registry.register_tool(
        base::container::String("resume_workflow"),
        base::container::String("Resume a paused workflow."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID of the paused workflow")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                bool success = engine->resume(execution_id);
                
                Json result;
                result["success"] = success;
                result["execution_id"] = execution_id;
                result["message"] = success ? "Workflow resumed successfully" : "Failed to resume workflow";
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("resume_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 8. cancel_workflow - 取消工作流
    registry.register_tool(
        base::container::String("cancel_workflow"),
        base::container::String("Cancel a running or paused workflow."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID of the workflow to cancel")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                bool success = engine->cancel(execution_id);
                
                Json result;
                result["success"] = success;
                result["execution_id"] = execution_id;
                result["message"] = success ? "Workflow cancelled successfully" : "Failed to cancel workflow";
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("cancel_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    // 9. get_workflow_metrics - 获取性能指标
    registry.register_tool(
        base::container::String("get_workflow_metrics"),
        base::container::String("Get performance metrics of a workflow execution."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID of the workflow")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            try {
                auto execution_id = args.value("execution_id", "");
                auto m = metrics->get_metrics();
                
                Json result;
                result["workflow_id"] = m.workflow_id;
                result["execution_id"] = m.execution_id;
                result["total_duration_ms"] = m.total_duration.count();
                result["total_tokens"] = m.total_tokens;
                result["total_tool_calls"] = m.total_tool_calls;
                result["success"] = true;
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("get_workflow_metrics: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // ========== 高级工具 ==========
    
    // 10. add_workflow_task - 动态添加任务
    registry.register_tool(
        base::container::String("add_workflow_task"),
        base::container::String("Dynamically add a task to a running workflow. Use this to adapt the workflow based on intermediate results."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID of the running workflow")
            }},
            {base::container::String("task"), ToolParameterSchema{
                .type = base::container::String("object"),
                .description = base::container::String("Task definition with id, type, prompt, and optional depends_on")
            }},
            {base::container::String("after_task"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Insert this task after the specified task ID (optional)")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                auto task_json = args.value("task", Json::object());
                auto after_task = args.value("after_task", "");
                
                // 解析任务
                WorkflowTaskDefinition task;
                task.id = task_json.value("id", "");
                task.name = task_json.value("name", task.id);
                task.type = task_json.value("type", "function");
                task.prompt = task_json.value("prompt", "");
                
                if (task_json.contains("depends_on") && task_json["depends_on"].is_array()) {
                    for (const auto& dep : task_json["depends_on"]) {
                        task.depends_on.push_back(dep.get<std::string>());
                    }
                }
                
                if (task_json.contains("config")) {
                    task.config = task_json["config"];
                }
                
                // 添加任务到工作流
                bool success = engine->add_task(execution_id, task, after_task);
                
                Json result;
                result["success"] = success;
                result["execution_id"] = execution_id;
                result["task_id"] = task.id;
                result["message"] = success ? "Task added successfully" : "Failed to add task";
                
                log::info_fmt("task added: execution_id={}, task_id={}", execution_id, task.id);
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("add_workflow_task: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // 11. submit_approval - 提交审批结果
    registry.register_tool(
        base::container::String("submit_approval"),
        base::container::String("Submit approval result for a human approval task."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID")
            }},
            {base::container::String("task_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Task ID waiting for approval")
            }},
            {base::container::String("decision"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Approval decision: approve/reject/modify")
            }},
            {base::container::String("comment"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Approval comment (optional)")
            }},
            {base::container::String("modifications"), ToolParameterSchema{
                .type = base::container::String("object"),
                .description = base::container::String("Modifications if decision is 'modify' (optional)")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!approval) {
                return container::String("Error: Approval manager not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                auto task_id = args.value("task_id", "");
                auto decision = args.value("decision", "");
                auto comment = args.value("comment", "");
                auto modifications = args.value("modifications", Json::object());
                
                // 构建审批结果
                ApprovalResult result;
                result.decision = decision;
                result.comment = comment;
                result.modifications = modifications;
                result.timestamp = std::chrono::system_clock::now();
                
                // 提交审批
                bool success = approval->submit_approval(execution_id, task_id, result);
                
                Json response;
                response["success"] = success;
                response["execution_id"] = execution_id;
                response["task_id"] = task_id;
                response["decision"] = decision;
                response["message"] = success ? "Approval submitted successfully" : "Failed to submit approval";
                
                log::info_fmt("approval submitted: execution_id={}, task_id={}, decision={}", 
                              execution_id, task_id, decision);
                
                return container::String(response.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("submit_approval: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // 12. list_pending_approvals - 列出待审批任务
    registry.register_tool(
        base::container::String("list_pending_approvals"),
        base::container::String("List all pending approval tasks."),
        {
            {base::container::String("execution_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Execution ID to filter (optional)")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!approval) {
                return container::String("Error: Approval manager not initialized");
            }
            
            try {
                auto execution_id = args.value("execution_id", "");
                
                auto pending = approval->list_pending_approvals(execution_id);
                
                Json result;
                result["success"] = true;
                result["count"] = pending.size();
                result["approvals"] = Json::array();
                
                for (const auto& [exec_id, task_id] : pending) {
                    auto config = approval->get_approval_config(exec_id, task_id);
                    
                    Json approval;
                    approval["execution_id"] = exec_id;
                    approval["task_id"] = task_id;
                    
                    if (config) {
                        approval["message"] = config->message;
                        approval["timeout_seconds"] = config->timeout.count();
                        approval["options"] = config->options;
                    }
                    
                    result["approvals"].push_back(approval);
                }
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("list_pending_approvals: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // 13. export_workflow - 导出工作流定义
    registry.register_tool(
        base::container::String("export_workflow"),
        base::container::String("Export a workflow definition to JSON format."),
        {
            {base::container::String("workflow_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Workflow ID to export")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto workflow_id = args.value("workflow_id", "");
                
                auto workflow = engine->get_workflow(workflow_id);
                if (!workflow) {
                    return container::String(R"({"success": false, "error": "Workflow not found"})");
                }
                
                // 序列化工作流定义
                Json result;
                result["success"] = true;
                result["workflow"]["id"] = workflow->id;
                result["workflow"]["name"] = workflow->name;
                result["workflow"]["variables"] = workflow->variables;
                
                Json tasks = Json::array();
                for (const auto& task : workflow->tasks) {
                    Json task_json;
                    task_json["id"] = task.id;
                    task_json["name"] = task.name;
                    task_json["type"] = task.type;
                    task_json["prompt"] = task.prompt;
                    
                    if (!task.depends_on.empty()) {
                        Json deps = Json::array();
                        for (const auto& dep : task.depends_on) {
                            deps.push_back(dep);
                        }
                        task_json["depends_on"] = deps;
                    }
                    
                    if (!task.config.is_null()) {
                        task_json["config"] = task.config;
                    }
                    
                    tasks.push_back(task_json);
                }
                result["workflow"]["tasks"] = tasks;
                
                log::info_fmt("workflow exported: id={}", workflow_id);
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("export_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // 14. import_workflow - 导入工作流定义
    registry.register_tool(
        base::container::String("import_workflow"),
        base::container::String("Import a workflow definition from JSON format."),
        {
            {base::container::String("workflow_json"), ToolParameterSchema{
                .type = base::container::String("object"),
                .description = base::container::String("Workflow definition in JSON format")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto workflow_json = args.value("workflow_json", Json::object());
                
                // 解析工作流定义
                WorkflowDefinition workflow;
                workflow.id = workflow_json.value("id", "");
                workflow.name = workflow_json.value("name", "");
                workflow.variables = workflow_json.value("variables", Json::object());
                
                if (workflow_json.contains("tasks") && workflow_json["tasks"].is_array()) {
                    for (const auto& task_json : workflow_json["tasks"]) {
                        WorkflowTaskDefinition task;
                        task.id = task_json.value("id", "");
                        task.name = task_json.value("name", task.id);
                        task.type = task_json.value("type", "function");
                        task.prompt = task_json.value("prompt", "");
                        
                        if (task_json.contains("depends_on") && task_json["depends_on"].is_array()) {
                            for (const auto& dep : task_json["depends_on"]) {
                                task.depends_on.push_back(dep.get<std::string>());
                            }
                        }
                        
                        if (task_json.contains("config")) {
                            task.config = task_json["config"];
                        }
                        
                        workflow.tasks.push_back(task);
                    }
                }
                
                // 验证工作流
                auto validation = engine->validate_workflow(workflow);
                if (!validation.valid) {
                    Json error_result;
                    error_result["success"] = false;
                    error_result["error"] = validation.error;
                    return container::String(error_result.dump().c_str());
                }
                
                // 注册工作流（自动加命名空间前缀）
                auto namespaced_id = engine->register_workflow(workflow);
                
                Json result;
                result["success"] = true;
                result["workflow_id"] = namespaced_id;
                result["task_count"] = workflow.tasks.size();
                result["message"] = "Workflow imported successfully";
                
                log::info_fmt("workflow imported: id={}, tasks={}", workflow.id, workflow.tasks.size());
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("import_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );
    
    // 15. visualize_workflow - 可视化工作流
    registry.register_tool(
        base::container::String("visualize_workflow"),
        base::container::String("Generate a visual representation of a workflow in Mermaid format."),
        {
            {base::container::String("workflow_id"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Workflow ID to visualize")
            }},
            {base::container::String("format"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Output format: mermaid (default) or dot")
            }}
        },
        [engine, templates, metrics, approval](const Json& args) -> container::String {
            if (!engine) {
                return container::String("Error: Workflow system not initialized");
            }
            
            try {
                auto workflow_id = args.value("workflow_id", "");
                auto format = args.value("format", "mermaid");
                
                auto workflow = engine->get_workflow(workflow_id);
                if (!workflow) {
                    return container::String(R"({"success": false, "error": "Workflow not found"})");
                }
                
                // 生成可视化
                WorkflowVisualizer visualizer;
                std::string visualization;
                
                if (format == "dot") {
                    visualization = visualizer.to_dot(*workflow);
                } else {
                    visualization = visualizer.to_mermaid(*workflow);
                }
                
                Json result;
                result["success"] = true;
                result["workflow_id"] = workflow_id;
                result["format"] = format;
                result["visualization"] = visualization;
                
                return container::String(result.dump().c_str());
                
            } catch (const std::exception& e) {
                log::error_fmt("visualize_workflow: exception: {}", e.what());
                return container::String(("Error: " + std::string(e.what())).c_str());
            }
        }
    );

    log::info_fmt("registered 15 workflow tools");
}

}  // namespace ben_gear::tools