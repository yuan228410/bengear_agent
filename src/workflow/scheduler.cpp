#include "ben_gear/workflow/scheduler.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <algorithm>
#include <chrono>

namespace ben_gear {
namespace workflow {

WorkflowScheduler::WorkflowScheduler(
    DAG dag, 
    std::shared_ptr<TaskExecutor> executor,
    ErrorHandlingStrategy error_strategy,
    RetryPolicy retry_policy,
    std::shared_ptr<WorkflowProgressCallbacks> progress_callbacks,
    std::shared_ptr<MetricsCollector> metrics,
    WorkflowId workflow_id,
    std::string execution_id)
    : dag_(std::move(dag))
    , executor_(std::move(executor))
    , error_strategy_(error_strategy)
    , retry_policy_(retry_policy)
    , progress_callbacks_(std::move(progress_callbacks))
    , metrics_(std::move(metrics))
    , workflow_id_(std::move(workflow_id))
    , execution_id_(std::move(execution_id)) {
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

        std::unordered_set<TaskId> successful_tasks;
        std::unordered_set<TaskId> completed_tasks;
        {
            std::lock_guard lock(mutex_);
            successful_tasks = successful_task_ids_locked();
            completed_tasks = completed_tasks_;
        }
        auto ready_tasks = dag_.get_ready_tasks(successful_tasks);
        ready_tasks.erase(
            std::remove_if(ready_tasks.begin(), ready_tasks.end(), [&completed_tasks](const TaskId& task_id) {
                return completed_tasks.find(task_id) != completed_tasks.end();
            }),
            ready_tasks.end());

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
                progress_callbacks_->on_task_started(workflow_id_, execution_id_, task_id, static_cast<int>(dag_.size()));
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
            {
                std::lock_guard lock(mutex_);
                task_results_[task_id] = task_result;
                completed_tasks_.insert(task_id);
            }

            // 通知：任务完成
            if (progress_callbacks_) {
                progress_callbacks_->on_task_completed(workflow_id_, execution_id_, task_id, task_result);
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
                workflow_id_, execution_id_,
                static_cast<int>(completed_tasks_.size()),
                static_cast<int>(dag_.size()));
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    {
        std::lock_guard lock(mutex_);
        result.task_results = task_results_;
        result.completed_tasks = completed_tasks_.size();
    }
    size_t failed_count = 0;
    for (const auto& [_, task_result] : result.task_results) {
        if (!task_result.success) {
            ++failed_count;
        }
    }
    result.success = failed_count == 0;
    result.status = result.success ? WorkflowStatus::SUCCESS : WorkflowStatus::FAILED;
    result.total_tasks = dag_.size();
    result.failed_tasks = failed_count;
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
    if (retry_policy_.max_retries > 0) {
        return executor_->execute_task_with_retry(task, ctx, retry_policy_);
    }
    return executor_->execute_task(task, ctx);
}

std::vector<std::pair<TaskId, TaskResult>> WorkflowScheduler::execute_batch_tasks(
    const std::vector<TaskId>& task_ids,
    const std::map<TaskId, TaskResult>& completed_results) {

    std::vector<std::pair<TaskId, TaskResult>> results;
    results.reserve(task_ids.size());

    std::vector<TaskPtr> tasks;
    std::vector<TaskId> scheduled_ids;
    tasks.reserve(task_ids.size());
    scheduled_ids.reserve(task_ids.size());

    for (const auto& task_id : task_ids) {
        auto task = dag_.get_task(task_id);
        if (!task) {
            results.emplace_back(task_id, TaskResult::error("Task not found: " + task_id));
            continue;
        }
        tasks.push_back(std::move(task));
        scheduled_ids.push_back(task_id);
    }

    if (!tasks.empty()) {
        auto shared_results = std::make_shared<const std::map<TaskId, TaskResult>>(completed_results);
        TaskContext ctx_template;
        ctx_template.upstream_results = std::move(shared_results);
        auto batch_results = retry_policy_.max_retries > 0
            ? executor_->execute_batch_with_retry(tasks, ctx_template, retry_policy_)
            : executor_->execute_batch(tasks, ctx_template);
        for (size_t i = 0; i < scheduled_ids.size(); ++i) {
            if (i < batch_results.size()) {
                results.emplace_back(scheduled_ids[i], std::move(batch_results[i]));
            } else {
                results.emplace_back(scheduled_ids[i], TaskResult::error("batch execution returned missing result"));
            }
        }
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

    std::unordered_set<TaskId> successful_copy;
    std::unordered_set<TaskId> completed_copy;
    {
        std::lock_guard lock(mutex_);
        snapshot.completed_tasks = completed_tasks_.size();
        completed_copy = completed_tasks_;
        successful_copy = successful_task_ids_locked();
        size_t failed_count = 0;
        for (const auto& [id, result] : task_results_) {
            if (!result.success) failed_count++;
        }
        snapshot.failed_tasks = failed_count;
    }

    if (running_ && snapshot.completed_tasks < dag_.size()) {
        auto ready_tasks = dag_.get_ready_tasks(successful_copy);
        ready_tasks.erase(
            std::remove_if(ready_tasks.begin(), ready_tasks.end(), [&completed_copy](const TaskId& task_id) {
                return completed_copy.find(task_id) != completed_copy.end();
            }),
            ready_tasks.end());
        if (!ready_tasks.empty()) snapshot.current_task = ready_tasks[0];
    }

    return snapshot;
}

std::unordered_set<TaskId> WorkflowScheduler::successful_task_ids_locked() const {
    std::unordered_set<TaskId> successful_tasks;
    for (const auto& [task_id, task_result] : task_results_) {
        if (task_result.success) {
            successful_tasks.insert(task_id);
        }
    }
    return successful_tasks;
}

} // namespace workflow
} // namespace ben_gear
