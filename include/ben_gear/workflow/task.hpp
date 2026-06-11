#pragma once

#include "types.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <string>
#include <any>
#include <memory>

namespace ben_gear {
namespace workflow {

// 前向声明
namespace json {
    class Value;
}

// 任务结果
struct TaskResult {
    bool success = false;
    std::string error_message;
    std::any output;  // 使用 std::any 存储任意类型的结果
    
    // 便捷构造函数
    static TaskResult ok(std::any output = {}) {
        return TaskResult{true, "", std::move(output)};
    }
    
    static TaskResult error(const std::string& msg) {
        return TaskResult{false, msg, {}};
    }
};

// 任务上下文
struct TaskContext {
    TaskId task_id;
    /// 上游任务结果（shared_ptr 共享只读引用，零拷贝）
    std::shared_ptr<const std::map<TaskId, TaskResult>> upstream_results;

    /// 便捷构造：从 map 创建
    static TaskContext from_map(const TaskId& id,
                                const std::map<TaskId, TaskResult>& results) {
        TaskContext ctx;
        ctx.task_id = id;
        ctx.upstream_results = std::make_shared<const std::map<TaskId, TaskResult>>(results);
        return ctx;
    }

    /// 便捷构造：共享已有 map（零拷贝）
    static TaskContext from_shared(const TaskId& id,
                                   std::shared_ptr<const std::map<TaskId, TaskResult>> results) {
        TaskContext ctx;
        ctx.task_id = id;
        ctx.upstream_results = std::move(results);
        return ctx;
    }

    // 获取上游任务结果
    template<typename T>
    std::optional<T> get_upstream_result(const TaskId& task_id) const {
        if (!upstream_results) return std::nullopt;
        auto it = upstream_results->find(task_id);
        if (it != upstream_results->end() && it->second.success) {
            try {
                return std::any_cast<T>(it->second.output);
            } catch (const std::bad_any_cast&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
};

// 任务接口
class ITask {
public:
    virtual ~ITask() = default;
    
    // 执行任务
    virtual TaskResult execute(const TaskContext& ctx) = 0;
    
    // 获取任务 ID
    virtual TaskId id() const = 0;
    
    // 获取任务状态
    virtual TaskStatus status() const = 0;
    
    // 设置任务状态
    virtual void set_status(TaskStatus status) = 0;
};

// 任务智能指针
using TaskPtr = std::shared_ptr<ITask>;

// 函数任务（Lambda 封装）
class FunctionTask : public ITask {
public:
    using TaskFunc = std::function<TaskResult(const TaskContext&)>;
    
    explicit FunctionTask(const TaskId& id, TaskFunc func)
        : id_(id), func_(std::move(func)), status_(TaskStatus::PENDING) {}
    
    TaskResult execute(const TaskContext& ctx) override {
        set_status(TaskStatus::RUNNING);
        try {
            auto result = func_(ctx);
            set_status(result.success ? TaskStatus::SUCCESS : TaskStatus::FAILED);
            return result;
        } catch (const std::exception& e) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error(e.what());
        }
    }
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
private:
    TaskId id_;
    TaskFunc func_;
    TaskStatus status_;
};

// 任务工厂
class TaskFactory {
public:
    // 创建函数任务
    static TaskPtr create_function_task(const TaskId& id, 
                                        FunctionTask::TaskFunc func) {
        return std::make_shared<FunctionTask>(id, std::move(func));
    }
};

} // namespace workflow
} // namespace ben_gear
