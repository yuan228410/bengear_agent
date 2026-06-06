#pragma once

#include "types.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <string>
#include <memory>
#include <map>
#include <functional>

namespace ben_gear {
namespace workflow {

/// 任务执行器接口（自定义任务类型）
class ITaskExecutor {
public:
    virtual ~ITaskExecutor() = default;
    
    /// 执行任务
    /// @param task 任务定义
    /// @param ctx 任务上下文
    /// @return 任务结果
    virtual TaskResult execute(
        const WorkflowTaskDefinition& task,
        const TaskContext& ctx) = 0;
    
    /// 任务类型名称
    virtual std::string type() const = 0;
    
    /// 验证任务配置
    virtual bool validate(const WorkflowTaskDefinition& task) const {
        return true;
    }
    
    /// 获取任务描述
    virtual std::string description() const {
        return "";
    }
};

/// 任务执行器注册表
class TaskExecutorRegistry {
public:
    /// 注册执行器
    void register_executor(std::unique_ptr<ITaskExecutor> executor) {
        if (!executor) {
            return;
        }
        
        std::string type = executor->type();
        executors_[type] = std::move(executor);
        
        log::info_fmt("task executor registered: type={}", type);
    }
    
    /// 获取执行器
    ITaskExecutor* get_executor(const std::string& type) const {
        auto it = executors_.find(type);
        if (it != executors_.end()) {
            return it->second.get();
        }
        return nullptr;
    }
    
    /// 检查执行器是否存在
    bool has_executor(const std::string& type) const {
        return executors_.find(type) != executors_.end();
    }
    
    /// 列出所有执行器类型
    std::vector<std::string> list_executors() const {
        std::vector<std::string> types;
        for (const auto& [type, _] : executors_) {
            types.push_back(type);
        }
        return types;
    }
    
    /// 执行任务
    TaskResult execute_task(
        const WorkflowTaskDefinition& task,
        const TaskContext& ctx) const {
        
        auto* executor = get_executor(task.type);
        if (!executor) {
            return TaskResult::error("Unknown task type: " + task.type);
        }
        
        // 验证任务配置
        if (!executor->validate(task)) {
            return TaskResult::error("Task validation failed: " + task.id);
        }
        
        return executor->execute(task, ctx);
    }
    
private:
    std::map<std::string, std::unique_ptr<ITaskExecutor>> executors_;
};

/// 示例：HTTP 请求任务执行器
class HttpTaskExecutor : public ITaskExecutor {
public:
    TaskResult execute(
        const WorkflowTaskDefinition& task,
        const TaskContext& ctx) override {
        
        // 解析配置
        std::string url = task.config.value("url", "");
        std::string method = task.config.value("method", "GET");
        Json headers = task.config.value("headers", Json::object());
        Json body = task.config.value("body", Json::object());
        int timeout = task.config.value("timeout", 30);
        
        // TODO: 实际的 HTTP 请求逻辑
        // 这里返回模拟结果
        Json output;
        output["status"] = 200;
        output["url"] = url;
        output["method"] = method;
        output["mock"] = true;
        
        return TaskResult::ok(output);
    }
    
    std::string type() const override {
        return "http";
    }
    
    std::string description() const override {
        return "HTTP request task executor";
    }
    
    bool validate(const WorkflowTaskDefinition& task) const override {
        return task.config.contains("url");
    }
};

/// 示例：数据库查询任务执行器
class DatabaseTaskExecutor : public ITaskExecutor {
public:
    TaskResult execute(
        const WorkflowTaskDefinition& task,
        const TaskContext& ctx) override {
        
        std::string query = task.config.value("query", "");
        std::string connection = task.config.value("connection", "");
        
        // TODO: 实际的数据库查询逻辑
        Json output;
        output["rows"] = Json::array();
        output["affected"] = 0;
        output["mock"] = true;
        
        return TaskResult::ok(output);
    }
    
    std::string type() const override {
        return "database";
    }
    
    std::string description() const override {
        return "Database query task executor";
    }
    
    bool validate(const WorkflowTaskDefinition& task) const override {
        return task.config.contains("query");
    }
};

/// 示例：脚本执行任务执行器
class ScriptTaskExecutor : public ITaskExecutor {
public:
    TaskResult execute(
        const WorkflowTaskDefinition& task,
        const TaskContext& ctx) override {
        
        std::string script = task.prompt;
        std::string interpreter = task.config.value("interpreter", "/bin/bash");
        int timeout = task.config.value("timeout", 60);
        
        // TODO: 实际的脚本执行逻辑
        Json output;
        output["stdout"] = "Script executed successfully";
        output["stderr"] = "";
        output["exit_code"] = 0;
        output["mock"] = true;
        
        return TaskResult::ok(output);
    }
    
    std::string type() const override {
        return "script";
    }
    
    std::string description() const override {
        return "Script execution task executor";
    }
};

} // namespace workflow
} // namespace ben_gear
