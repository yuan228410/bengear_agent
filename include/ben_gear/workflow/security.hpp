#pragma once

#include "ben_gear/base/utils/json.hpp"
#include <string>
#include <map>
#include <set>
#include <vector>
#include <chrono>
#include <mutex>
#include <optional>

namespace ben_gear {
namespace workflow {

/// 工作流权限
enum class WorkflowPermission {
    execute,    // 执行工作流
    create,     // 创建工作流
    modify,     // 修改工作流
    delete_,    // 删除工作流
    admin       // 管理权限（授予他人权限）
};

/// 权限管理器
class WorkflowPermissionManager {
public:
    /// 检查权限
    bool has_permission(
        const std::string& user_id,
        const std::string& workflow_id,
        WorkflowPermission permission) const {
        
        std::shared_lock lock(mutex_);
        
        // 检查用户是否有 admin 权限
        if (has_admin_permission(user_id)) {
            return true;
        }
        
        // 检查工作流特定权限
        auto workflow_it = workflow_permissions_.find(workflow_id);
        if (workflow_it != workflow_permissions_.end()) {
            auto user_it = workflow_it->second.find(user_id);
            if (user_it != workflow_it->second.end()) {
                return user_it->second.count(permission) > 0;
            }
        }
        
        // 检查全局权限
        auto global_it = global_permissions_.find(user_id);
        if (global_it != global_permissions_.end()) {
            return global_it->second.count(permission) > 0;
        }
        
        return false;
    }
    
    /// 授予权限
    void grant(
        const std::string& user_id,
        const std::string& workflow_id,
        WorkflowPermission permission) {
        
        std::unique_lock lock(mutex_);
        workflow_permissions_[workflow_id][user_id].insert(permission);
        
        log::info_fmt("permission granted: user={}, workflow={}, permission={}", 
                      user_id, workflow_id, static_cast<int>(permission));
    }
    
    /// 授予全局权限
    void grant_global(
        const std::string& user_id,
        WorkflowPermission permission) {
        
        std::unique_lock lock(mutex_);
        global_permissions_[user_id].insert(permission);
        
        log::info_fmt("global permission granted: user={}, permission={}", 
                      user_id, static_cast<int>(permission));
    }
    
    /// 撤销权限
    void revoke(
        const std::string& user_id,
        const std::string& workflow_id,
        WorkflowPermission permission) {
        
        std::unique_lock lock(mutex_);
        
        auto workflow_it = workflow_permissions_.find(workflow_id);
        if (workflow_it != workflow_permissions_.end()) {
            auto user_it = workflow_it->second.find(user_id);
            if (user_it != workflow_it->second.end()) {
                user_it->second.erase(permission);
            }
        }
        
        log::info_fmt("permission revoked: user={}, workflow={}, permission={}", 
                      user_id, workflow_id, static_cast<int>(permission));
    }
    
    /// 获取用户的所有权限
    std::set<WorkflowPermission> get_permissions(
        const std::string& user_id,
        const std::string& workflow_id) const {
        
        std::shared_lock lock(mutex_);
        std::set<WorkflowPermission> result;
        
        // 合并全局权限
        auto global_it = global_permissions_.find(user_id);
        if (global_it != global_permissions_.end()) {
            result.insert(global_it->second.begin(), global_it->second.end());
        }
        
        // 合并工作流特定权限
        auto workflow_it = workflow_permissions_.find(workflow_id);
        if (workflow_it != workflow_permissions_.end()) {
            auto user_it = workflow_it->second.find(user_id);
            if (user_it != workflow_it->second.end()) {
                result.insert(user_it->second.begin(), user_it->second.end());
            }
        }
        
        return result;
    }
    
    /// 设置管理员
    void set_admin(const std::string& user_id) {
        std::unique_lock lock(mutex_);
        admins_.insert(user_id);
        
        log::info_fmt("admin set: user={}", user_id);
    }
    
    /// 移除管理员
    void remove_admin(const std::string& user_id) {
        std::unique_lock lock(mutex_);
        admins_.erase(user_id);
        
        log::info_fmt("admin removed: user={}", user_id);
    }
    
    /// 检查是否是管理员
    bool is_admin(const std::string& user_id) const {
        std::shared_lock lock(mutex_);
        return admins_.count(user_id) > 0;
    }
    
private:
    bool has_admin_permission(const std::string& user_id) const {
        return admins_.count(user_id) > 0;
    }
    
private:
    mutable std::shared_mutex mutex_;
    
    // 管理员列表
    std::set<std::string> admins_;
    
    // 全局权限：user_id -> permissions
    std::map<std::string, std::set<WorkflowPermission>> global_permissions_;
    
    // 工作流特定权限：workflow_id -> (user_id -> permissions)
    std::map<std::string, std::map<std::string, std::set<WorkflowPermission>>> workflow_permissions_;
};

/// 工作流审计日志
struct WorkflowAuditLog {
    std::string execution_id;                           // 执行ID
    std::string user_id;                                // 操作用户
    std::string action;                                 // 操作类型：start/pause/resume/cancel/complete
    std::string workflow_id;                            // 工作流ID
    std::string task_id;                                // 任务ID（可选）
    Json details;                                       // 详细信息
    std::chrono::system_clock::time_point timestamp;    // 时间戳
    
    /// 转换为 JSON
    Json to_json() const {
        Json j;
        j["execution_id"] = execution_id;
        j["user_id"] = user_id;
        j["action"] = action;
        j["workflow_id"] = workflow_id;
        j["task_id"] = task_id;
        j["details"] = details;
        j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            timestamp.time_since_epoch()).count();
        return j;
    }
    
    /// 从 JSON 解析
    static WorkflowAuditLog from_json(const Json& j) {
        WorkflowAuditLog log;
        log.execution_id = j.value("execution_id", "");
        log.user_id = j.value("user_id", "");
        log.action = j.value("action", "");
        log.workflow_id = j.value("workflow_id", "");
        log.task_id = j.value("task_id", "");
        log.details = j.value("details", Json::object());
        log.timestamp = std::chrono::system_clock::time_point(
            std::chrono::seconds(j.value("timestamp", 0))
        );
        return log;
    }
};

/// 审计日志管理器
class WorkflowAuditLogger {
public:
    /// 记录审计日志
    void log(const WorkflowAuditLog& entry) {
        std::unique_lock lock(mutex_);
        
        logs_.push_back(entry);
        
        // 按工作流ID索引
        if (!entry.workflow_id.empty()) {
            workflow_index_[entry.workflow_id].push_back(logs_.size() - 1);
        }
        
        // 按用户ID索引
        if (!entry.user_id.empty()) {
            user_index_[entry.user_id].push_back(logs_.size() - 1);
        }
        
        log::debug_fmt("audit log recorded: user={}, action={}, workflow={}", 
                       entry.user_id, entry.action, entry.workflow_id);
    }
    
    /// 记录操作（便捷方法）
    void log_action(
        const std::string& user_id,
        const std::string& action,
        const std::string& workflow_id = "",
        const std::string& execution_id = "",
        const std::string& task_id = "",
        const Json& details = {}) {
        
        WorkflowAuditLog entry;
        entry.user_id = user_id;
        entry.action = action;
        entry.workflow_id = workflow_id;
        entry.execution_id = execution_id;
        entry.task_id = task_id;
        entry.details = details;
        entry.timestamp = std::chrono::system_clock::now();
        
        log(entry);
    }
    
    /// 查询审计日志（按工作流ID）
    std::vector<WorkflowAuditLog> query_by_workflow(
        const std::string& workflow_id,
        std::chrono::system_clock::time_point from = {},
        std::chrono::system_clock::time_point to = std::chrono::system_clock::now()) const {
        
        std::shared_lock lock(mutex_);
        std::vector<WorkflowAuditLog> result;
        
        auto it = workflow_index_.find(workflow_id);
        if (it != workflow_index_.end()) {
            for (size_t idx : it->second) {
                if (idx < logs_.size()) {
                    const auto& log_entry = logs_[idx];
                    if (log_entry.timestamp >= from && log_entry.timestamp <= to) {
                        result.push_back(log_entry);
                    }
                }
            }
        }
        
        return result;
    }
    
    /// 查询审计日志（按用户ID）
    std::vector<WorkflowAuditLog> query_by_user(
        const std::string& user_id,
        std::chrono::system_clock::time_point from = {},
        std::chrono::system_clock::time_point to = std::chrono::system_clock::now()) const {
        
        std::shared_lock lock(mutex_);
        std::vector<WorkflowAuditLog> result;
        
        auto it = user_index_.find(user_id);
        if (it != user_index_.end()) {
            for (size_t idx : it->second) {
                if (idx < logs_.size()) {
                    const auto& log_entry = logs_[idx];
                    if (log_entry.timestamp >= from && log_entry.timestamp <= to) {
                        result.push_back(log_entry);
                    }
                }
            }
        }
        
        return result;
    }
    
    /// 查询最近的日志
    std::vector<WorkflowAuditLog> query_recent(size_t limit = 100) const {
        std::shared_lock lock(mutex_);
        
        std::vector<WorkflowAuditLog> result;
        size_t start = logs_.size() > limit ? logs_.size() - limit : 0;
        
        for (size_t i = start; i < logs_.size(); ++i) {
            result.push_back(logs_[i]);
        }
        
        return result;
    }
    
    /// 清理旧日志
    size_t cleanup_old_logs(std::chrono::hours max_age = std::chrono::hours(24 * 30)) {
        std::unique_lock lock(mutex_);
        
        auto cutoff = std::chrono::system_clock::now() - max_age;
        size_t removed = 0;
        
        // 简化实现：重建日志列表
        std::vector<WorkflowAuditLog> new_logs;
        for (const auto& entry : logs_) {
            if (entry.timestamp >= cutoff) {
                new_logs.push_back(entry);
            } else {
                removed++;
            }
        }
        
        logs_ = std::move(new_logs);
        
        // 重建索引
        rebuild_indexes();
        
        log::info_fmt("cleaned up {} old audit logs", removed);
        
        return removed;
    }
    
    /// 导出日志
    bool export_logs(const std::string& path) const {
        std::shared_lock lock(mutex_);
        
        try {
            Json j = Json::array();
            for (const auto& entry : logs_) {
                j.push_back(entry.to_json());
            }
            
            std::ofstream file(path);
            if (!file.is_open()) {
                return false;
            }
            
            file << j.dump(2);
            return true;
            
        } catch (const std::exception& e) {
            log::error_fmt("failed to export audit logs: {}", e.what());
            return false;
        }
    }
    
    /// 导入日志
    bool import_logs(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return false;
            }
            
            Json j;
            file >> j;
            
            std::unique_lock lock(mutex_);
            
            for (const auto& entry_json : j) {
                logs_.push_back(WorkflowAuditLog::from_json(entry_json));
            }
            
            rebuild_indexes();
            
            log::info_fmt("imported {} audit logs from {}", logs_.size(), path);
            return true;
            
        } catch (const std::exception& e) {
            log::error_fmt("failed to import audit logs: {}", e.what());
            return false;
        }
    }
    
    /// 获取统计信息
    struct AuditStats {
        size_t total_logs;
        size_t logs_by_action;
        std::map<std::string, size_t> action_counts;
        std::map<std::string, size_t> user_counts;
    };
    
    AuditStats get_stats() const {
        std::shared_lock lock(mutex_);
        
        AuditStats stats;
        stats.total_logs = logs_.size();
        
        for (const auto& entry : logs_) {
            stats.action_counts[entry.action]++;
            stats.user_counts[entry.user_id]++;
        }
        
        return stats;
    }
    
private:
    void rebuild_indexes() {
        workflow_index_.clear();
        user_index_.clear();
        
        for (size_t i = 0; i < logs_.size(); ++i) {
            const auto& entry = logs_[i];
            
            if (!entry.workflow_id.empty()) {
                workflow_index_[entry.workflow_id].push_back(i);
            }
            
            if (!entry.user_id.empty()) {
                user_index_[entry.user_id].push_back(i);
            }
        }
    }
    
private:
    mutable std::shared_mutex mutex_;
    
    std::vector<WorkflowAuditLog> logs_;
    
    // 索引：workflow_id -> log indices
    std::map<std::string, std::vector<size_t>> workflow_index_;
    
    // 索引：user_id -> log indices
    std::map<std::string, std::vector<size_t>> user_index_;
};

} // namespace workflow
} // namespace ben_gear
