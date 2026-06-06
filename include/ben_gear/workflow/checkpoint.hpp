#pragma once

#include "types.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <optional>

namespace ben_gear {
namespace workflow {

/// 工作流检查点
struct WorkflowCheckpoint {
    std::string execution_id;
    std::string workflow_id;
    std::string current_task_id;
    WorkflowState state;
    std::chrono::system_clock::time_point created_at;
    
    /// 序列化到文件
    bool save(const std::filesystem::path& path) const {
        try {
            Json j;
            j["execution_id"] = execution_id;
            j["workflow_id"] = workflow_id;
            j["current_task_id"] = current_task_id;
            j["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                created_at.time_since_epoch()).count();
            
            // 序列化状态
            j["state"]["id"] = state.id;
            j["state"]["status"] = static_cast<int>(state.status);
            j["state"]["error_message"] = state.error_message;
            
            // 序列化任务结果
            Json task_results;
            for (const auto& [task_id, result] : state.task_results) {
                Json task_result;
                task_result["success"] = result.success;
                task_result["error_message"] = result.error_message;
                // 注意：std::any 不能直接序列化，这里简化处理
                // 实际应用中需要根据类型处理
                task_results[task_id] = task_result;
            }
            j["state"]["task_results"] = task_results;
            
            // 写入文件
            std::ofstream file(path);
            if (!file.is_open()) {
                return false;
            }
            
            file << j.dump(2);
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    /// 从文件恢复
    static std::optional<WorkflowCheckpoint> load(const std::filesystem::path& path) {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return std::nullopt;
            }
            
            Json j;
            file >> j;
            
            WorkflowCheckpoint checkpoint;
            checkpoint.execution_id = j.value("execution_id", "");
            checkpoint.workflow_id = j.value("workflow_id", "");
            checkpoint.current_task_id = j.value("current_task_id", "");
            checkpoint.created_at = std::chrono::system_clock::time_point(
                std::chrono::seconds(j.value("created_at", 0))
            );
            
            // 恢复状态
            checkpoint.state.id = j["state"].value("id", "");
            checkpoint.state.status = static_cast<WorkflowStatus>(j["state"].value("status", 0));
            checkpoint.state.error_message = j["state"].value("error_message", "");
            
            // 恢复任务结果
            if (j["state"].contains("task_results") && j["state"]["task_results"].is_object()) {
                for (auto& [task_id, result_json] : j["state"]["task_results"].items()) {
                    TaskResult result;
                    result.success = result_json.value("success", false);
                    result.error_message = result_json.value("error_message", "");
                    checkpoint.state.task_results[task_id] = result;
                }
            }
            
            return checkpoint;
            
        } catch (const std::exception& e) {
            return std::nullopt;
        }
    }
};

/// 检查点管理器
class CheckpointManager {
public:
    explicit CheckpointManager(const std::filesystem::path& checkpoint_dir)
        : checkpoint_dir_(checkpoint_dir) {
        // 创建检查点目录
        std::error_code ec;
        std::filesystem::create_directories(checkpoint_dir_, ec);
    }
    
    /// 保存检查点
    bool save_checkpoint(const WorkflowCheckpoint& checkpoint) {
        auto path = get_checkpoint_path(checkpoint.execution_id);
        bool success = checkpoint.save(path);
        
        if (success) {
            log::info_fmt("checkpoint saved: execution_id={}, path={}", 
                          checkpoint.execution_id, path.string());
        } else {
            log::error_fmt("failed to save checkpoint: execution_id={}", 
                           checkpoint.execution_id);
        }
        
        return success;
    }
    
    /// 加载检查点
    std::optional<WorkflowCheckpoint> load_checkpoint(const std::string& execution_id) {
        auto path = get_checkpoint_path(execution_id);
        auto checkpoint = WorkflowCheckpoint::load(path);
        
        if (checkpoint) {
            log::info_fmt("checkpoint loaded: execution_id={}", execution_id);
        } else {
            log::error_fmt("failed to load checkpoint: execution_id={}", execution_id);
        }
        
        return checkpoint;
    }
    
    /// 删除检查点
    bool delete_checkpoint(const std::string& execution_id) {
        auto path = get_checkpoint_path(execution_id);
        std::error_code ec;
        bool success = std::filesystem::remove(path, ec);
        
        if (success) {
            log::info_fmt("checkpoint deleted: execution_id={}", execution_id);
        }
        
        return success;
    }
    
    /// 列出所有检查点
    std::vector<std::string> list_checkpoints() const {
        std::vector<std::string> checkpoints;
        
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir_, ec)) {
            if (entry.path().extension() == ".checkpoint") {
                checkpoints.push_back(entry.path().stem().string());
            }
        }
        
        return checkpoints;
    }
    
    /// 清理过期检查点
    size_t cleanup_old_checkpoints(std::chrono::hours max_age = std::chrono::hours(24 * 7)) {
        size_t deleted = 0;
        auto now = std::chrono::system_clock::now();
        
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir_, ec)) {
            if (entry.path().extension() == ".checkpoint") {
                // 加载检查点检查时间
                auto checkpoint = WorkflowCheckpoint::load(entry.path());
                if (checkpoint) {
                    auto age = std::chrono::duration_cast<std::chrono::hours>(
                        now - checkpoint->created_at);
                    if (age > max_age) {
                        std::filesystem::remove(entry.path(), ec);
                        deleted++;
                    }
                }
            }
        }
        
        if (deleted > 0) {
            log::info_fmt("cleaned up {} old checkpoints", deleted);
        }
        
        return deleted;
    }
    
private:
    std::filesystem::path get_checkpoint_path(const std::string& execution_id) const {
        return checkpoint_dir_ / (execution_id + ".checkpoint");
    }
    
    std::filesystem::path checkpoint_dir_;
};

/// 自动检查点策略
struct AutoCheckpointPolicy {
    bool enabled = false;
    int interval = 1;  // 每隔 N 个任务保存一次
    bool on_failure = true;  // 失败时保存
    bool on_pause = true;    // 暂停时保存
};

} // namespace workflow
} // namespace ben_gear
