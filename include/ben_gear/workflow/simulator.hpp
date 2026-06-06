#pragma once

#include "types.hpp"
#include "workflow_engine.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <map>
#include <functional>

namespace ben_gear {
namespace workflow {

/// 模拟执行模式
enum class SimulationMode {
    dry_run,        // 不实际执行，只验证流程
    mock_llm,       // 模拟 LLM 返回
    mock_tools,     // 模拟工具返回
    record,         // 记录执行过程（用于回放）
    replay          // 回放执行过程
};

/// 模拟输出配置
struct MockOutputs {
    std::map<std::string, Json> task_outputs;  // task_id -> mock output
    std::map<std::string, std::string> task_errors;  // task_id -> mock error
    
    /// 设置任务的模拟输出
    void set_output(const std::string& task_id, const Json& output) {
        task_outputs[task_id] = output;
    }
    
    /// 设置任务的模拟错误
    void set_error(const std::string& task_id, const std::string& error) {
        task_errors[task_id] = error;
    }
    
    /// 获取任务的模拟结果
    std::optional<TaskResult> get_result(const std::string& task_id) const {
        auto error_it = task_errors.find(task_id);
        if (error_it != task_errors.end()) {
            return TaskResult::error(error_it->second);
        }
        
        auto output_it = task_outputs.find(task_id);
        if (output_it != task_outputs.end()) {
            return TaskResult::ok(output_it->second);
        }
        
        return std::nullopt;
    }
};

/// 执行记录
struct ExecutionRecord {
    std::string task_id;
    std::string task_type;
    TaskContext context;
    TaskResult result;
    std::chrono::milliseconds duration;
    std::chrono::system_clock::time_point timestamp;
};

/// 工作流模拟器
class WorkflowSimulator {
public:
    explicit WorkflowSimulator(std::shared_ptr<WorkflowEngine> engine)
        : engine_(engine) {}
    
    /// 模拟执行工作流
    WorkflowState simulate(
        const WorkflowDefinition& workflow,
        SimulationMode mode,
        const MockOutputs& mock_outputs = {}) {
        
        log::info_fmt("workflow simulation started: mode={}, workflow={}", 
                      static_cast<int>(mode), workflow.id);
        
        switch (mode) {
            case SimulationMode::dry_run:
                return dry_run(workflow);
                
            case SimulationMode::mock_llm:
                return mock_execute(workflow, mock_outputs, true, false);
                
            case SimulationMode::mock_tools:
                return mock_execute(workflow, mock_outputs, false, true);
                
            case SimulationMode::record:
                return record_execute(workflow);
                
            case SimulationMode::replay:
                return replay_execute(workflow);
                
            default:
                return WorkflowState{};
        }
    }
    
    /// 单步执行（调试模式）
    std::optional<TaskResult> step(
        const std::string& execution_id,
        const std::string& task_id) {
        
        auto state = engine_->get_state(execution_id);
        if (!state) {
            log::error_fmt("step failed: execution not found: {}", execution_id);
            return std::nullopt;
        }
        
        // 检查任务是否可执行
        // TODO: 实现单步执行逻辑
        
        log::info_fmt("step executed: execution_id={}, task_id={}", execution_id, task_id);
        
        return std::nullopt;
    }
    
    /// 设置断点
    void set_breakpoint(const std::string& task_id) {
        breakpoints_.insert(task_id);
        log::info_fmt("breakpoint set: task_id={}", task_id);
    }
    
    /// 清除断点
    void clear_breakpoint(const std::string& task_id) {
        breakpoints_.erase(task_id);
        log::info_fmt("breakpoint cleared: task_id={}", task_id);
    }
    
    /// 清除所有断点
    void clear_all_breakpoints() {
        breakpoints_.clear();
        log::info_fmt("all breakpoints cleared");
    }
    
    /// 检查是否命中断点
    bool is_breakpoint(const std::string& task_id) const {
        return breakpoints_.count(task_id) > 0;
    }
    
    /// 获取执行记录
    const std::vector<ExecutionRecord>& get_records() const {
        return records_;
    }
    
    /// 保存执行记录
    bool save_records(const std::string& path) const {
        try {
            Json j = Json::array();
            for (const auto& record : records_) {
                Json record_json;
                record_json["task_id"] = record.task_id;
                record_json["task_type"] = record.task_type;
                record_json["duration_ms"] = record.duration.count();
                record_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                    record.timestamp.time_since_epoch()).count();
                
                // 简化：不保存 context 和 result 的完整内容
                j.push_back(record_json);
            }
            
            std::ofstream file(path);
            if (!file.is_open()) {
                return false;
            }
            
            file << j.dump(2);
            return true;
            
        } catch (const std::exception& e) {
            log::error_fmt("failed to save records: {}", e.what());
            return false;
        }
    }
    
    /// 加载执行记录
    bool load_records(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return false;
            }
            
            Json j;
            file >> j;
            
            records_.clear();
            for (const auto& record_json : j) {
                ExecutionRecord record;
                record.task_id = record_json.value("task_id", "");
                record.task_type = record_json.value("task_type", "");
                record.duration = std::chrono::milliseconds(record_json.value("duration_ms", 0));
                record.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::seconds(record_json.value("timestamp", 0))
                );
                records_.push_back(record);
            }
            
            log::info_fmt("loaded {} execution records from {}", records_.size(), path);
            return true;
            
        } catch (const std::exception& e) {
            log::error_fmt("failed to load records: {}", e.what());
            return false;
        }
    }
    
private:
    /// Dry run（只验证流程）
    WorkflowState dry_run(const WorkflowDefinition& workflow) {
        WorkflowState state;
        state.id = "dry_run_" + workflow.id;
        state.status = WorkflowStatus::SUCCESS;
        
        // 验证工作流定义
        auto validation = engine_->validate_workflow(workflow);
        if (!validation.valid) {
            state.status = WorkflowStatus::FAILED;
            state.error_message = validation.error;
            return state;
        }
        
        // 模拟所有任务成功
        for (const auto& task : workflow.tasks) {
            TaskResult result;
            result.success = true;
            state.task_results[task.id] = result;
        }
        
        log::info_fmt("dry run completed: workflow={}", workflow.id);
        
        return state;
    }
    
    /// Mock 执行
    WorkflowState mock_execute(
        const WorkflowDefinition& workflow,
        const MockOutputs& mock_outputs,
        bool mock_llm,
        bool mock_tools) {
        
        WorkflowState state;
        state.id = "mock_" + workflow.id;
        state.status = WorkflowStatus::RUNNING;
        
        for (const auto& task : workflow.tasks) {
            // 检查是否需要 mock
            bool should_mock = (mock_llm && task.type == "llm") ||
                              (mock_tools && task.type == "tool");
            
            TaskResult result;
            
            if (should_mock) {
                // 使用 mock 输出
                auto mock_result = mock_outputs.get_result(task.id);
                if (mock_result) {
                    result = *mock_result;
                } else {
                    // 默认 mock 输出
                    Json default_output;
                    default_output["mock"] = true;
                    default_output["task_id"] = task.id;
                    result = TaskResult::ok(default_output);
                }
            } else {
                // 实际执行
                // TODO: 调用实际执行器
                result = TaskResult::ok();
            }
            
            state.task_results[task.id] = result;
        }
        
        state.status = WorkflowStatus::SUCCESS;
        
        log::info_fmt("mock execute completed: workflow={}", workflow.id);
        
        return state;
    }
    
    /// 记录执行
    WorkflowState record_execute(const WorkflowDefinition& workflow) {
        records_.clear();
        
        // 执行并记录
        auto state = engine_->execute(workflow.id);
        
        // 记录每个任务
        for (const auto& [task_id, result] : state.task_results) {
            ExecutionRecord record;
            record.task_id = task_id;
            record.timestamp = std::chrono::system_clock::now();
            record.result = result;
            records_.push_back(record);
        }
        
        log::info_fmt("record execute completed: workflow={}, records={}", 
                      workflow.id, records_.size());
        
        return state;
    }
    
    /// 回放执行
    WorkflowState replay_execute(const WorkflowDefinition& workflow) {
        WorkflowState state;
        state.id = "replay_" + workflow.id;
        state.status = WorkflowStatus::SUCCESS;
        
        // 从记录中恢复
        for (const auto& record : records_) {
            state.task_results[record.task_id] = record.result;
        }
        
        log::info_fmt("replay execute completed: workflow={}", workflow.id);
        
        return state;
    }
    
private:
    std::shared_ptr<WorkflowEngine> engine_;
    std::set<std::string> breakpoints_;
    std::vector<ExecutionRecord> records_;
};

} // namespace workflow
} // namespace ben_gear
