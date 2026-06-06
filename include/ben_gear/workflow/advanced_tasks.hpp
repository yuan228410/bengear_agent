#pragma once

#include "task.hpp"
#include "types.hpp"
#include <functional>
#include <regex>
#include <stdexcept>

namespace ben_gear {
namespace workflow {

/// 条件配置
struct ConditionConfig {
    std::string expression;        // 条件表达式（支持变量引用）
    std::string true_branch;       // true 时执行的任务ID
    std::string false_branch;      // false 时执行的任务ID
};

/// 条件任务（根据表达式选择执行路径）
class ConditionTask : public ITask {
public:
    ConditionTask(
        const TaskId& id,
        const ConditionConfig& config)
        : id_(id)
        , config_(config)
        , status_(TaskStatus::PENDING)
        , selected_branch_(std::nullopt) {}
    
    /// 执行条件判断
    TaskResult execute(const TaskContext& ctx) override {
        set_status(TaskStatus::RUNNING);
        
        try {
            // 1. 解析表达式中的变量
            std::string resolved_expr = resolve_expression(config_.expression, ctx);
            
            // 2. 评估表达式
            bool result = evaluate_expression(resolved_expr);
            
            // 3. 选择分支
            selected_branch_ = result ? config_.true_branch : config_.false_branch;
            
            set_status(TaskStatus::SUCCESS);
            
            // 返回选中的分支信息
            Json output;
            output["condition_result"] = result;
            output["selected_branch"] = *selected_branch_;
            output["expression"] = config_.expression;
            
            return TaskResult::ok(output);
            
        } catch (const std::exception& e) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error(std::string("Condition evaluation failed: ") + e.what());
        }
    }
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
    /// 获取选中的分支
    std::optional<std::string> selected_branch() const { return selected_branch_; }
    
private:
    /// 解析表达式中的变量
    std::string resolve_expression(const std::string& expr, const TaskContext& ctx) {
        std::string result = expr;
        
        // 替换上游任务结果
        for (const auto& [task_id, task_result] : ctx.upstream_results) {
            std::string placeholder = "{" + task_id + "}";
            if (result.find(placeholder) != std::string::npos && task_result.success) {
                try {
                    // 尝试从 JSON 输出中提取值
                    if (task_result.output.type() == typeid(Json)) {
                        auto json_output = std::any_cast<Json>(task_result.output);
                        // 如果是简单值，直接替换
                        if (json_output.is_string() || json_output.is_number() || json_output.is_boolean()) {
                            result = replace_all(result, placeholder, json_output.dump());
                        }
                    } else if (task_result.output.type() == typeid(std::string)) {
                        auto str_output = std::any_cast<std::string>(task_result.output);
                        result = replace_all(result, placeholder, str_output);
                    }
                } catch (const std::bad_any_cast&) {
                    // 忽略类型不匹配
                }
            }
        }
        
        return result;
    }
    
    /// 评估表达式
    bool evaluate_expression(const std::string& expr) {
        // 简单的表达式解析器
        // 支持：==, !=, >, <, >=, <=, &&, ||, !
        // 以及：contains, starts_with, ends_with
        
        std::string trimmed = trim(expr);
        
        // 处理逻辑运算符
        size_t or_pos = find_operator(trimmed, "||");
        if (or_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, or_pos));
            std::string right = trim(trimmed.substr(or_pos + 2));
            return evaluate_expression(left) || evaluate_expression(right);
        }
        
        size_t and_pos = find_operator(trimmed, "&&");
        if (and_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, and_pos));
            std::string right = trim(trimmed.substr(and_pos + 2));
            return evaluate_expression(left) && evaluate_expression(right);
        }
        
        if (trimmed[0] == '!') {
            return !evaluate_expression(trimmed.substr(1));
        }
        
        // 处理比较运算符
        size_t eq_pos = trimmed.find("==");
        if (eq_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, eq_pos));
            std::string right = trim(trimmed.substr(eq_pos + 2));
            return compare_values(left, right, "==");
        }
        
        size_t ne_pos = trimmed.find("!=");
        if (ne_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, ne_pos));
            std::string right = trim(trimmed.substr(ne_pos + 2));
            return compare_values(left, right, "!=");
        }
        
        size_t ge_pos = trimmed.find(">=");
        if (ge_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, ge_pos));
            std::string right = trim(trimmed.substr(ge_pos + 2));
            return compare_values(left, right, ">=");
        }
        
        size_t le_pos = trimmed.find("<=");
        if (le_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, le_pos));
            std::string right = trim(trimmed.substr(le_pos + 2));
            return compare_values(left, right, "<=");
        }
        
        size_t gt_pos = trimmed.find(">");
        if (gt_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, gt_pos));
            std::string right = trim(trimmed.substr(gt_pos + 1));
            return compare_values(left, right, ">");
        }
        
        size_t lt_pos = trimmed.find("<");
        if (lt_pos != std::string::npos) {
            std::string left = trim(trimmed.substr(0, lt_pos));
            std::string right = trim(trimmed.substr(lt_pos + 1));
            return compare_values(left, right, "<");
        }
        
        // 处理函数调用
        size_t func_pos = trimmed.find('(');
        if (func_pos != std::string::npos) {
            std::string func_name = trim(trimmed.substr(0, func_pos));
            size_t end_pos = trimmed.rfind(')');
            if (end_pos != std::string::npos) {
                std::string args = trim(trimmed.substr(func_pos + 1, end_pos - func_pos - 1));
                return evaluate_function(func_name, args);
            }
        }
        
        // 直接返回布尔值
        if (trimmed == "true" || trimmed == "1") return true;
        if (trimmed == "false" || trimmed == "0") return false;
        
        // 非空字符串为 true
        return !trimmed.empty() && trimmed != "null" && trimmed != "\"\"";
    }
    
    /// 比较值
    bool compare_values(const std::string& left, const std::string& right, const std::string& op) {
        // 移除引号
        std::string l = unquote(left);
        std::string r = unquote(right);
        
        // 尝试数值比较
        try {
            double l_num = std::stod(l);
            double r_num = std::stod(r);
            
            if (op == "==") return l_num == r_num;
            if (op == "!=") return l_num != r_num;
            if (op == ">") return l_num > r_num;
            if (op == "<") return l_num < r_num;
            if (op == ">=") return l_num >= r_num;
            if (op == "<=") return l_num <= r_num;
        } catch (...) {
            // 不是数值，进行字符串比较
        }
        
        // 字符串比较
        if (op == "==") return l == r;
        if (op == "!=") return l != r;
        
        throw std::runtime_error("Invalid comparison operator for strings: " + op);
    }
    
    /// 评估函数
    bool evaluate_function(const std::string& func_name, const std::string& args) {
        // contains(string, substring)
        if (func_name == "contains") {
            size_t comma_pos = args.find(',');
            if (comma_pos != std::string::npos) {
                std::string str = unquote(trim(args.substr(0, comma_pos)));
                std::string substr = unquote(trim(args.substr(comma_pos + 1)));
                return str.find(substr) != std::string::npos;
            }
        }
        
        // starts_with(string, prefix)
        if (func_name == "starts_with") {
            size_t comma_pos = args.find(',');
            if (comma_pos != std::string::npos) {
                std::string str = unquote(trim(args.substr(0, comma_pos)));
                std::string prefix = unquote(trim(args.substr(comma_pos + 1)));
                return str.substr(0, prefix.size()) == prefix;
            }
        }
        
        // ends_with(string, suffix)
        if (func_name == "ends_with") {
            size_t comma_pos = args.find(',');
            if (comma_pos != std::string::npos) {
                std::string str = unquote(trim(args.substr(0, comma_pos)));
                std::string suffix = unquote(trim(args.substr(comma_pos + 1)));
                if (suffix.size() > str.size()) return false;
                return str.substr(str.size() - suffix.size()) == suffix;
            }
        }
        
        // is_empty(value)
        if (func_name == "is_empty") {
            std::string value = unquote(trim(args));
            return value.empty() || value == "null";
        }
        
        // is_number(value)
        if (func_name == "is_number") {
            std::string value = trim(args);
            try {
                std::stod(value);
                return true;
            } catch (...) {
                return false;
            }
        }
        
        throw std::runtime_error("Unknown function: " + func_name);
    }
    
    /// 查找运算符位置（忽略引号内的）
    size_t find_operator(const std::string& str, const std::string& op) {
        bool in_quotes = false;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '"') {
                in_quotes = !in_quotes;
            } else if (!in_quotes && str.substr(i, op.size()) == op) {
                return i;
            }
        }
        return std::string::npos;
    }
    
    /// 去除首尾空白
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }
    
    /// 去除引号
    std::string unquote(const std::string& str) {
        std::string trimmed = trim(str);
        if (trimmed.size() >= 2 && trimmed[0] == '"' && trimmed[trimmed.size() - 1] == '"') {
            return trimmed.substr(1, trimmed.size() - 2);
        }
        return trimmed;
    }
    
    /// 替换所有
    std::string replace_all(const std::string& str, const std::string& from, const std::string& to) {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }
    
private:
    TaskId id_;
    ConditionConfig config_;
    TaskStatus status_;
    std::optional<std::string> selected_branch_;
};

/// 子工作流任务配置
struct SubflowConfig {
    std::string workflow_id;              // 子工作流ID
    Json inputs;                         // 输入变量
    bool inherit_variables = true;       // 是否继承父工作流变量
};

/// 子工作流任务（嵌套执行另一个工作流）
class SubflowTask : public ITask {
public:
    SubflowTask(
        const TaskId& id,
        std::shared_ptr<WorkflowEngine> engine,
        const SubflowConfig& config)
        : id_(id)
        , engine_(engine)
        , config_(config)
        , status_(TaskStatus::PENDING) {}
    
    /// 执行子工作流
    TaskResult execute(const TaskContext& ctx) override {
        set_status(TaskStatus::RUNNING);
        
        try {
            // 1. 获取子工作流定义
            auto workflow_opt = engine_->get_workflow(config_.workflow_id);
            if (!workflow_opt) {
                set_status(TaskStatus::FAILED);
                return TaskResult::error("Subflow not found: " + config_.workflow_id);
            }
            
            auto subflow = *workflow_opt;
            
            // 2. 合并变量
            if (config_.inherit_variables) {
                // 从上下文中获取父工作流变量（如果有）
                // 这里简化处理，实际需要从 WorkflowState 中获取
            }
            
            // 合并传入的输入变量
            for (auto& [key, value] : config_.inputs.items()) {
                subflow.variables[key] = value;
            }
            
            // 3. 解析输入变量中的引用
            resolve_inputs(subflow, ctx);
            
            // 4. 执行子工作流
            auto state = engine_->execute(config_.workflow_id);
            
            // 5. 返回结果
            if (state.status == WorkflowStatus::SUCCESS) {
                set_status(TaskStatus::SUCCESS);
                
                Json output;
                output["workflow_id"] = config_.workflow_id;
                output["execution_id"] = state.id;
                output["status"] = static_cast<int>(state.status);
                
                // 包含所有任务结果
                Json task_results;
                for (const auto& [task_id, result] : state.task_results) {
                    task_results[task_id] = result.success;
                }
                output["task_results"] = task_results;
                
                return TaskResult::ok(output);
            } else {
                set_status(TaskStatus::FAILED);
                return TaskResult::error("Subflow execution failed: " + state.error_message);
            }
            
        } catch (const std::exception& e) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error(std::string("Subflow execution error: ") + e.what());
        }
    }
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
private:
    /// 解析输入变量
    void resolve_inputs(WorkflowDefinition& subflow, const TaskContext& ctx) {
        // 替换任务 prompt 中的变量引用
        for (auto& task : subflow.tasks) {
            task.prompt = resolve_variables(task.prompt, ctx);
        }
    }
    
    /// 解析变量
    std::string resolve_variables(const std::string& str, const TaskContext& ctx) {
        std::string result = str;
        
        for (const auto& [task_id, task_result] : ctx.upstream_results) {
            std::string placeholder = "{" + task_id + "}";
            if (result.find(placeholder) != std::string::npos && task_result.success) {
                try {
                    auto output = std::any_cast<std::string>(task_result.output);
                    result = replace_all(result, placeholder, output);
                } catch (const std::bad_any_cast&) {
                    // 忽略
                }
            }
        }
        
        return result;
    }
    
    std::string replace_all(const std::string& str, const std::string& from, const std::string& to) {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }
    
private:
    TaskId id_;
    std::shared_ptr<WorkflowEngine> engine_;
    SubflowConfig config_;
    TaskStatus status_;
};

/// 并行任务配置
struct ParallelConfig {
    std::vector<WorkflowTaskDefinition> subtasks;  // 子任务列表
    bool fail_fast = true;                        // 是否快速失败
};

/// 任务工厂扩展（添加新任务类型）
class TaskFactoryEx {
public:
    /// 创建条件任务
    static TaskPtr create_condition_task(
        const TaskId& id,
        const ConditionConfig& config) {
        return std::make_shared<ConditionTask>(id, config);
    }
    
    /// 创建子工作流任务
    static TaskPtr create_subflow_task(
        const TaskId& id,
        std::shared_ptr<WorkflowEngine> engine,
        const SubflowConfig& config) {
        return std::make_shared<SubflowTask>(id, engine, config);
    }
    
    // ... 之前的 LLM/Tool 任务创建方法
};

} // namespace workflow
} // namespace ben_gear
