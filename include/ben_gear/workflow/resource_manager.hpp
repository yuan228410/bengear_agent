#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace ben_gear {
namespace workflow {

/// 资源限制配置
struct ResourceLimits {
    int max_parallel_tasks = 5;         // 最大并行任务数
    int max_llm_concurrent = 3;         // 最大并发 LLM 调用
    int max_tool_concurrent = 10;       // 最大并发工具执行
    size_t max_memory_mb = 1024;        // 最大内存使用（MB）
    std::chrono::seconds max_duration{3600};  // 最大执行时长
};

/// 资源使用情况
struct ResourceUsage {
    int active_tasks = 0;               // 当前活跃任务数
    int active_llm_calls = 0;           // 当前 LLM 调用数
    int active_tool_calls = 0;          // 当前工具调用数
    size_t memory_used_mb = 0;          // 当前内存使用（MB）
    std::chrono::seconds elapsed_time{0};  // 已执行时长
};

/// 资源管理器
class ResourceManager {
public:
    explicit ResourceManager(const ResourceLimits& limits = {})
        : limits_(limits)
        , active_tasks_(0)
        , active_llm_calls_(0)
        , active_tool_calls_(0) {}
    
    /// 设置资源限制
    void set_limits(const ResourceLimits& limits) {
        std::unique_lock lock(mutex_);
        limits_ = limits;
    }
    
    /// 获取资源限制
    ResourceLimits get_limits() const {
        std::shared_lock lock(mutex_);
        return limits_;
    }
    
    /// 获取当前资源使用情况
    ResourceUsage get_usage() const {
        ResourceUsage usage;
        usage.active_tasks = active_tasks_.load();
        usage.active_llm_calls = active_llm_calls_.load();
        usage.active_tool_calls = active_tool_calls_.load();
        usage.elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_);
        // 内存使用需要平台相关实现，这里简化
        usage.memory_used_mb = 0;
        return usage;
    }
    
    /// 尝试获取任务资源
    bool try_acquire_task() {
        int expected = active_tasks_.load();
        while (expected < limits_.max_parallel_tasks) {
            if (active_tasks_.compare_exchange_weak(expected, expected + 1)) {
                return true;
            }
        }
        return false;
    }
    
    /// 释放任务资源
    void release_task() {
        active_tasks_.fetch_sub(1);
        // 通知等待的任务
        task_available_.notify_all();
    }
    
    /// 等待任务资源可用
    void wait_for_task_slot() {
        std::unique_lock lock(mutex_);
        task_available_.wait(lock, [this] {
            return active_tasks_.load() < limits_.max_parallel_tasks;
        });
    }
    
    /// 尝试获取 LLM 资源
    bool try_acquire_llm() {
        int expected = active_llm_calls_.load();
        while (expected < limits_.max_llm_concurrent) {
            if (active_llm_calls_.compare_exchange_weak(expected, expected + 1)) {
                return true;
            }
        }
        return false;
    }
    
    /// 释放 LLM 资源
    void release_llm() {
        active_llm_calls_.fetch_sub(1);
        llm_available_.notify_all();
    }
    
    /// 等待 LLM 资源可用
    void wait_for_llm_slot() {
        std::unique_lock lock(mutex_);
        llm_available_.wait(lock, [this] {
            return active_llm_calls_.load() < limits_.max_llm_concurrent;
        });
    }
    
    /// 尝试获取工具资源
    bool try_acquire_tool() {
        int expected = active_tool_calls_.load();
        while (expected < limits_.max_tool_concurrent) {
            if (active_tool_calls_.compare_exchange_weak(expected, expected + 1)) {
                return true;
            }
        }
        return false;
    }
    
    /// 释放工具资源
    void release_tool() {
        active_tool_calls_.fetch_sub(1);
        tool_available_.notify_all();
    }
    
    /// 等待工具资源可用
    void wait_for_tool_slot() {
        std::unique_lock lock(mutex_);
        tool_available_.wait(lock, [this] {
            return active_tool_calls_.load() < limits_.max_tool_concurrent;
        });
    }
    
    /// 检查是否超时
    bool is_timeout() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_);
        return elapsed > limits_.max_duration;
    }
    
    /// 检查资源是否可用
    bool has_resources() const {
        return active_tasks_.load() < limits_.max_parallel_tasks;
    }
    
    /// 获取可用任务槽位数
    int available_task_slots() const {
        return limits_.max_parallel_tasks - active_tasks_.load();
    }
    
private:
    ResourceLimits limits_;
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    
    std::atomic<int> active_tasks_;
    std::atomic<int> active_llm_calls_;
    std::atomic<int> active_tool_calls_;
    
    mutable std::shared_mutex mutex_;
    std::condition_variable_any task_available_;
    std::condition_variable_any llm_available_;
    std::condition_variable_any tool_available_;
};

/// 资源守卫（RAII）
class TaskResourceGuard {
public:
    explicit TaskResourceGuard(std::shared_ptr<ResourceManager> manager)
        : manager_(manager) {
        if (manager_) {
            manager_->wait_for_task_slot();
        }
    }
    
    ~TaskResourceGuard() {
        if (manager_) {
            manager_->release_task();
        }
    }
    
    // 禁止拷贝
    TaskResourceGuard(const TaskResourceGuard&) = delete;
    TaskResourceGuard& operator=(const TaskResourceGuard&) = delete;
    
private:
    std::shared_ptr<ResourceManager> manager_;
};

/// LLM 资源守卫
class LLMResourceGuard {
public:
    explicit LLMResourceGuard(std::shared_ptr<ResourceManager> manager)
        : manager_(manager) {
        if (manager_) {
            manager_->wait_for_llm_slot();
        }
    }
    
    ~LLMResourceGuard() {
        if (manager_) {
            manager_->release_llm();
        }
    }
    
    LLMResourceGuard(const LLMResourceGuard&) = delete;
    LLMResourceGuard& operator=(const LLMResourceGuard&) = delete;
    
private:
    std::shared_ptr<ResourceManager> manager_;
};

/// 工具资源守卫
class ToolResourceGuard {
public:
    explicit ToolResourceGuard(std::shared_ptr<ResourceManager> manager)
        : manager_(manager) {
        if (manager_) {
            manager_->wait_for_tool_slot();
        }
    }
    
    ~ToolResourceGuard() {
        if (manager_) {
            manager_->release_tool();
        }
    }
    
    ToolResourceGuard(const ToolResourceGuard&) = delete;
    ToolResourceGuard& operator=(const ToolResourceGuard&) = delete;
    
private:
    std::shared_ptr<ResourceManager> manager_;
};

} // namespace workflow
} // namespace ben_gear
