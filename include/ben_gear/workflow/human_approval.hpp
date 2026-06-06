#pragma once

#include "types.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <string>
#include <optional>
#include <chrono>
#include <mutex>
#include <condition_variable>

namespace ben_gear {
namespace workflow {

/// 人工审批任务配置
struct HumanApprovalConfig {
    std::string message;                    // 审批提示信息
    Json context;                          // 审批上下文
    std::chrono::seconds timeout{3600};    // 超时时间（默认1小时）
    std::vector<std::string> options{"approve", "reject", "modify"};  // 审批选项
    bool required = true;                  // 是否必须审批
};

/// 审批结果
struct ApprovalResult {
    std::string decision;                  // 审批决定（approve/reject/modify）
    Json modifications;                    // 修改内容（如果 decision == "modify"）
    std::string comment;                   // 审批意见
    std::chrono::system_clock::time_point timestamp;
};

/// 人工审批任务
class HumanApprovalTask : public ITask {
public:
    HumanApprovalTask(
        const TaskId& id,
        const HumanApprovalConfig& config)
        : id_(id)
        , config_(config)
        , status_(TaskStatus::PENDING)
        , approval_received_(false) {}
    
    /// 执行审批任务（等待审批）
    TaskResult execute(const TaskContext& ctx) override {
        (void)ctx;  // 避免未使用参数告警
        set_status(TaskStatus::RUNNING);
        
        // 等待审批结果
        std::unique_lock lock(mutex_);
        bool timeout = !condition_.wait_for(
            lock, 
            config_.timeout,
            [this] { return approval_received_.load(); }
        );
        
        if (timeout) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error("Approval timeout");
        }
        
        if (!approval_result_) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error("No approval result");
        }
        
        // 根据审批结果决定任务状态
        if (approval_result_->decision == "approve") {
            set_status(TaskStatus::SUCCESS);
            
            Json output;
            output["decision"] = "approve";
            output["comment"] = approval_result_->comment;
            
            return TaskResult::ok(output);
            
        } else if (approval_result_->decision == "reject") {
            set_status(TaskStatus::FAILED);
            return TaskResult::error("Approval rejected: " + approval_result_->comment);
            
        } else if (approval_result_->decision == "modify") {
            set_status(TaskStatus::SUCCESS);
            
            Json output;
            output["decision"] = "modify";
            output["modifications"] = approval_result_->modifications;
            output["comment"] = approval_result_->comment;
            
            return TaskResult::ok(output);
        }
        
        set_status(TaskStatus::FAILED);
        return TaskResult::error("Invalid approval decision");
    }
    
    /// 提交审批结果
    bool submit_approval(const ApprovalResult& result) {
        // 验证审批选项
        bool valid_option = false;
        for (const auto& option : config_.options) {
            if (result.decision == option) {
                valid_option = true;
                break;
            }
        }
        
        if (!valid_option) {
            return false;
        }
        
        {
            std::unique_lock lock(mutex_);
            approval_result_ = result;
            approval_received_ = true;
        }
        
        condition_.notify_one();
        return true;
    }
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
    /// 获取审批配置
    const HumanApprovalConfig& config() const { return config_; }
    
    /// 是否需要审批
    bool is_waiting() const {
        return status_ == TaskStatus::RUNNING && !approval_received_.load();
    }
    
private:
    TaskId id_;
    HumanApprovalConfig config_;
    TaskStatus status_;
    
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> approval_received_;
    std::optional<ApprovalResult> approval_result_;
};

/// 审批管理器
class ApprovalManager {
public:
    /// 注册审批任务
    void register_approval_task(
        const std::string& execution_id,
        const std::string& task_id,
        std::shared_ptr<HumanApprovalTask> task) {
        
        std::unique_lock lock(mutex_);
        pending_approvals_[execution_id][task_id] = task;
        
        log::info_fmt("approval task registered: execution_id={}, task_id={}", 
                      execution_id, task_id);
    }
    
    /// 提交审批结果
    bool submit_approval(
        const std::string& execution_id,
        const std::string& task_id,
        const ApprovalResult& result) {
        
        std::shared_ptr<HumanApprovalTask> task;
        
        {
            std::shared_lock lock(mutex_);
            auto exec_it = pending_approvals_.find(execution_id);
            if (exec_it == pending_approvals_.end()) {
                log::error_fmt("approval submission failed: execution not found: {}", execution_id);
                return false;
            }
            
            auto task_it = exec_it->second.find(task_id);
            if (task_it == exec_it->second.end()) {
                log::error_fmt("approval submission failed: task not found: {}", task_id);
                return false;
            }
            
            task = task_it->second;
        }
        
        if (!task) {
            return false;
        }
        
        bool success = task->submit_approval(result);
        
        if (success) {
            log::info_fmt("approval submitted: execution_id={}, task_id={}, decision={}", 
                          execution_id, task_id, result.decision);
            
            // 移除已完成的审批
            std::unique_lock lock(mutex_);
            pending_approvals_[execution_id].erase(task_id);
            if (pending_approvals_[execution_id].empty()) {
                pending_approvals_.erase(execution_id);
            }
        }
        
        return success;
    }
    
    /// 获取待审批任务列表
    std::vector<std::pair<std::string, std::string>> list_pending_approvals(
        const std::string& execution_id = "") const {
        
        std::vector<std::pair<std::string, std::string>> result;
        
        std::shared_lock lock(mutex_);
        
        if (execution_id.empty()) {
            // 返回所有待审批任务
            for (const auto& [exec_id, tasks] : pending_approvals_) {
                for (const auto& [task_id, task] : tasks) {
                    if (task && task->is_waiting()) {
                        result.emplace_back(exec_id, task_id);
                    }
                }
            }
        } else {
            // 返回指定执行的待审批任务
            auto it = pending_approvals_.find(execution_id);
            if (it != pending_approvals_.end()) {
                for (const auto& [task_id, task] : it->second) {
                    if (task && task->is_waiting()) {
                        result.emplace_back(execution_id, task_id);
                    }
                }
            }
        }
        
        return result;
    }
    
    /// 获取审批任务详情
    std::optional<HumanApprovalConfig> get_approval_config(
        const std::string& execution_id,
        const std::string& task_id) const {
        
        std::shared_lock lock(mutex_);
        
        auto exec_it = pending_approvals_.find(execution_id);
        if (exec_it == pending_approvals_.end()) {
            return std::nullopt;
        }
        
        auto task_it = exec_it->second.find(task_id);
        if (task_it == exec_it->second.end()) {
            return std::nullopt;
        }
        
        if (!task_it->second) {
            return std::nullopt;
        }
        
        return task_it->second->config();
    }
    
    /// 取消审批任务
    bool cancel_approval(const std::string& execution_id, const std::string& task_id) {
        std::unique_lock lock(mutex_);
        
        auto exec_it = pending_approvals_.find(execution_id);
        if (exec_it == pending_approvals_.end()) {
            return false;
        }
        
        auto task_it = exec_it->second.find(task_id);
        if (task_it == exec_it->second.end()) {
            return false;
        }
        
        // 提交拒绝结果以解除等待
        ApprovalResult reject;
        reject.decision = "reject";
        reject.comment = "Approval cancelled";
        reject.timestamp = std::chrono::system_clock::now();
        
        if (task_it->second) {
            task_it->second->submit_approval(reject);
        }
        
        exec_it->second.erase(task_it);
        
        log::info_fmt("approval cancelled: execution_id={}, task_id={}", execution_id, task_id);
        
        return true;
    }
    
private:
    mutable std::shared_mutex mutex_;
    // execution_id -> (task_id -> task)
    std::map<std::string, std::map<std::string, std::shared_ptr<HumanApprovalTask>>> pending_approvals_;
};

} // namespace workflow
} // namespace ben_gear
