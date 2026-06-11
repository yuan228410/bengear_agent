#include "ben_gear/workflow/human_approval.hpp"
#include <shared_mutex>
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear {
namespace workflow {

void ApprovalManager::register_approval_task(
    const std::string& execution_id,
    const std::string& task_id,
    std::shared_ptr<HumanApprovalTask> task) {

    std::unique_lock lock(mutex_);
    pending_approvals_[execution_id][task_id] = task;
    log::info_fmt("approval task registered: execution_id={}, task_id={}", execution_id, task_id);
}

bool ApprovalManager::submit_approval(
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

    if (!task) return false;

    bool success = task->submit_approval(result);
    if (success) {
        log::info_fmt("approval submitted: execution_id={}, task_id={}, decision={}",
                      execution_id, task_id, result.decision);
        std::unique_lock lock(mutex_);
        pending_approvals_[execution_id].erase(task_id);
        if (pending_approvals_[execution_id].empty()) {
            pending_approvals_.erase(execution_id);
        }
    }
    return success;
}

std::vector<std::pair<std::string, std::string>> ApprovalManager::list_pending_approvals(
    const std::string& execution_id) const {

    std::vector<std::pair<std::string, std::string>> result;
    std::shared_lock lock(mutex_);

    if (execution_id.empty()) {
        for (const auto& [exec_id, tasks] : pending_approvals_) {
            for (const auto& [task_id, task] : tasks) {
                if (task && task->is_waiting()) {
                    result.emplace_back(exec_id, task_id);
                }
            }
        }
    } else {
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

std::optional<HumanApprovalConfig> ApprovalManager::get_approval_config(
    const std::string& execution_id,
    const std::string& task_id) const {

    std::shared_lock lock(mutex_);
    auto exec_it = pending_approvals_.find(execution_id);
    if (exec_it == pending_approvals_.end()) return std::nullopt;
    auto task_it = exec_it->second.find(task_id);
    if (task_it == exec_it->second.end()) return std::nullopt;
    if (!task_it->second) return std::nullopt;
    return task_it->second->config();
}

bool ApprovalManager::cancel_approval(const std::string& execution_id, const std::string& task_id) {
    std::unique_lock lock(mutex_);
    auto exec_it = pending_approvals_.find(execution_id);
    if (exec_it == pending_approvals_.end()) return false;
    auto task_it = exec_it->second.find(task_id);
    if (task_it == exec_it->second.end()) return false;

    ApprovalResult reject;
    reject.decision = "reject";
    reject.comment = "Approval cancelled";
    reject.timestamp = std::chrono::system_clock::now();

    if (task_it->second) {
        task_it->second->submit_approval(reject);
    }
    exec_it->second.erase(task_id);
    log::info_fmt("approval cancelled: execution_id={}, task_id={}", execution_id, task_id);
    return true;
}

} // namespace workflow
} // namespace ben_gear
