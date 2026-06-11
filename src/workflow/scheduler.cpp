#include "ben_gear/workflow/scheduler.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <chrono>

namespace ben_gear {
namespace workflow {

WorkflowScheduler::WorkflowScheduler(
    DAG dag, 
    std::shared_ptr<TaskExecutor> executor,
    ErrorHandlingStrategy error_strategy,
    std::shared_ptr<WorkflowProgressCallbacks> progress_callbacks,
    std::shared_ptr<MetricsCollector> metrics)
    : dag_(std::move(dag))
    , executor_(std::move(executor))
    , error_strategy_(error_strategy)
    , progress_callbacks_(std::move(progress_callbacks))
    , metrics_(std::move(metrics)) {
}

WorkflowResult WorkflowScheduler::run() {
    running_ = true;
    paused_ = false;
    cancelled_ = false;

    auto start_time = std::chrono::steady_clock::now();

    WorkflowResult result;
    result.status = WorkflowStatus::RUNNING;

    if (dag_.empty()) {
        result.success = true;
        result.status = WorkflowStatus::SUCCESS;
        running_ = false;
        return result;
    }

    if (dag_.has_cycle()) {
        result.success = false;
        result.error_message = "DAG contains cycle";
        result.status = WorkflowStatus::FAILED;
        running_ = false;
        log::error_fmt("workflow scheduler: DAG contains cycle, aborting");
        return result;
    }

    log::info_fmt("workflow scheduler: starting, total_tasks={}", dag_.size());

    while (completed_tasks_.size() < dag_.size()) {
        if (should_stop()) {
            result.status = cancelled_ ? WorkflowStatus::CANCELLED : WorkflowStatus::PAUSED;
            running_ = false;
            log::info_fmt("workflow scheduler: stopped, status={}, completed={}/{}",
                          static_cast<int>(result.status), completed_tasks_.size(), dag_.size());
            return result;
        }

        auto ready_tasks = dag_.get_ready_tasks(completed_tasks_);

        if (ready_tasks.empty()) {
            result.success = false;
            result.error_message = "Some tasks failed, blocking downstream tasks";
            result.status = WorkflowStatus::FAILED;
            running_ = false;
            log::error_fmt("workflow scheduler: no ready tasks but incomplete, completed={}/{}",
                           completed_tasks_.size(), dag_.size());
            return result;
        }

        // 通知：每个就绪任务开始
        if (progress_callbacks_) {
            for (const auto& task_id : ready_tasks) {
                progress_callbacks_->on_task_started(task_id, static_cast<int>(dag_.size()));
            }
        }
        if (metrics_) {
            for (const auto& task_id : ready_tasks) {
                auto task = dag_.get_task(task_id);
                metrics_->record_task_start(task_id, task ? "task" : "unknown");
            }
        }

        log::info_fmt("workflow scheduler: executing batch, tasks=[{}], completed={}/{}",
                      [&]{ std::string s; for (auto& t : ready_tasks) { if (!s.empty()) s += ","; s += t; } return s; }(),
                      completed_tasks_.size(), dag_.size());

        auto batch_results = execute_batch_tasks(ready_tasks, task_results_);

        for (const auto& [task_id, task_result] : batch_results) {
            task_results_[task_id] = task_result;
            completed_tasks_.insert(task_id);

            // 通知：任务完成
            if (progress_callbacks_) {
                progress_callbacks_->on_task_completed(task_id, task_result);
            }
            if (metrics_) {
                metrics_->record_task_complete(task_id, task_result);
            }

            if (!task_result.success) {
                log::error_fmt("workflow scheduler: task failed, id={}, error={}",
                               task_id, task_result.error_message);
                if (error_strategy_ == ErrorHandlingStrategy::FAIL_FAST) {
                    result.success = false;
                    result.error_message = "Task failed: " + task_id + " - " + task_result.error_message;
                    result.status = WorkflowStatus::FAILED;
                    running_ = false;
                    return result;
                }
            } else {
                log::info_fmt("workflow scheduler: task completed, id={}", task_id);
            }
        }

        // 通知：整体进度
        if (progress_callbacks_) {
            progress_callbacks_->on_workflow_progress(
                static_cast<int>(completed_tasks_.size()),
                static_cast<int>(dag_.size()));
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    result.success = true;
    result.task_results = task_results_;
    result.status = WorkflowStatus::SUCCESS;
    result.total_tasks = dag_.size();
    result.completed_tasks = completed_tasks_.size();
    result.failed_tasks = 0;
    result.execution_time_ms = static_cast<uint64_t>(duration_ms);

    if (metrics_) {
        metrics_->set_total_duration(std::chrono::milliseconds(duration_ms));
        metrics_->calculate_cost();
    }

    running_ = false;
    log::info_fmt("workflow scheduler: all tasks completed, total={}, time={}ms",
                  dag_.size(), duration_ms);

    return result;
}

std::future<WorkflowResult> WorkflowScheduler::run_async() {
    return std::async(std::launch::async, [this]() -> WorkflowResult {
        return run();
    });
}

void WorkflowScheduler::pause() { paused_ = true; }
void WorkflowScheduler::resume() { paused_ = false; }
void WorkflowScheduler::cancel() { cancelled_ = true; }

TaskResult WorkflowScheduler::execute_single_task(
    const TaskId& task_id,
    const std::map<TaskId, TaskResult>& completed_results) {

    auto task = dag_.get_task(task_id);
    if (!task) return TaskResult::error("Task not found: " + task_id);

    auto ctx = TaskContext::from_map(task_id, completed_results);
    return executor_->execute_task(task, ctx);
}

std::vector<std::pair<TaskId, TaskResult>> WorkflowScheduler::execute_batch_tasks(
    const std::vector<TaskId>& task_ids,
    const std::map<TaskId, TaskResult>& completed_results) {

    std::vector<std::pair<TaskId, TaskResult>> results;
    results.reserve(task_ids.size());

    std::vector<TaskPtr> tasks;
    std::map<TaskPtr, TaskId> task_id_map;

    for (const auto& task_id : task_ids) {
        auto task = dag_.get_task(task_id);
        if (task) {
            tasks.push_back(task);
            task_id_map[task] = task_id;
        }
    }

    // 共享 upstream_results（零拷贝，并行任务只读访问同一份数据）
    auto shared_results = std::make_shared<const std::map<TaskId, TaskResult>>(completed_results);
    TaskContext ctx_template;
    ctx_template.upstream_results = shared_results;

    auto batch_results = executor_->execute_batch(tasks, ctx_template);

    for (size_t i = 0; i < tasks.size() && i < batch_results.size(); ++i) {
        results.emplace_back(task_id_map[tasks[i]], batch_results[i]);
    }

    return results;
}

bool WorkflowScheduler::should_stop() const {
    return cancelled_ || paused_;
}

WorkflowStatusSnapshot WorkflowScheduler::get_status() const {
    WorkflowStatusSnapshot snapshot;
    snapshot.running = running_.load();
    snapshot.total_tasks = dag_.size();
    snapshot.completed_tasks = completed_tasks_.size();

    size_t failed_count = 0;
    for (const auto& [id, result] : task_results_) {
        if (!result.success) failed_count++;
    }
    snapshot.failed_tasks = failed_count;

    if (running_ && completed_tasks_.size() < dag_.size()) {
        auto ready_tasks = dag_.get_ready_tasks(completed_tasks_);
        if (!ready_tasks.empty()) snapshot.current_task = ready_tasks[0];
    }

    return snapshot;
}

} // namespace workflow
} // namespace ben_gear
