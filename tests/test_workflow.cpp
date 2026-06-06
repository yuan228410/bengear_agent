#include <gtest/gtest.h>
#include "ben_gear/workflow/workflow_builder.hpp"
#include "ben_gear/workflow/workflow_runner.hpp"
#include "ben_gear/workflow/executor.hpp"
#include "ben_gear/workflow/scheduler.hpp"
#include "ben_gear/workflow/dag.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"

using namespace ben_gear::workflow;
using namespace ben_gear::base::concurrency;

// ==================== DAG 测试 ====================
class DAGTest : public ::testing::Test {
protected:
    DAG dag_;
};

TEST_F(DAGTest, EmptyDAG) {
    EXPECT_TRUE(dag_.empty());
    EXPECT_EQ(dag_.size(), 0);
}

TEST_F(DAGTest, AddTask) {
    auto task = TaskFactory::create_function_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    dag_.add_task("task1", task);
    EXPECT_FALSE(dag_.empty());
    EXPECT_EQ(dag_.size(), 1);
}

TEST_F(DAGTest, AddDependency) {
    auto task1 = TaskFactory::create_function_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    auto task2 = TaskFactory::create_function_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    dag_.add_task("task1", task1);
    dag_.add_task("task2", task2);
    dag_.add_dependency("task1", "task2");  // task2 依赖 task1
    
    EXPECT_FALSE(dag_.has_cycle());
}

TEST_F(DAGTest, DetectCycle) {
    auto task1 = TaskFactory::create_function_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    auto task2 = TaskFactory::create_function_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    auto task3 = TaskFactory::create_function_task("task3", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    dag_.add_task("task1", task1);
    dag_.add_task("task2", task2);
    dag_.add_task("task3", task3);
    
    // 创建链：task1 -> task2 -> task3
    dag_.add_dependency("task1", "task2");
    dag_.add_dependency("task2", "task3");
    
    // 尝试创建环：task3 -> task1（应该抛出异常）
    EXPECT_THROW(dag_.add_dependency("task3", "task1"), std::runtime_error);
    
    // 验证没有环
    EXPECT_FALSE(dag_.has_cycle());
}

TEST_F(DAGTest, GetReadyTasks) {
    auto task1 = TaskFactory::create_function_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    auto task2 = TaskFactory::create_function_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    dag_.add_task("task1", task1);
    dag_.add_task("task2", task2);
    dag_.add_dependency("task1", "task2");  // task2 依赖 task1
    
    std::unordered_set<TaskId> completed;
    auto ready = dag_.get_ready_tasks(completed);
    
    // 只有 task1 可以执行
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], "task1");
    
    // 标记 task1 完成
    completed.insert("task1");
    ready = dag_.get_ready_tasks(completed);
    
    // 现在 task2 可以执行
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], "task2");
}

// ==================== TaskExecutor 测试 ====================
class TaskExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto thread_pool = std::make_shared<ThreadPool>();
        executor_ = std::make_shared<TaskExecutor>(thread_pool);
    }
    
    std::shared_ptr<TaskExecutor> executor_;
};

TEST_F(TaskExecutorTest, ExecuteSimpleTask) {
    auto task = TaskFactory::create_function_task("test_task", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok(std::string("success"));
    });
    
    TaskContext ctx;
    ctx.task_id = "test_task";
    
    auto result = executor_->execute_task(task, ctx);
    EXPECT_TRUE(result.success);
}

TEST_F(TaskExecutorTest, ExecuteFailedTask) {
    auto task = TaskFactory::create_function_task("fail_task", [](const TaskContext& /*ctx*/) {
        return TaskResult::error("intentional failure");
    });
    
    TaskContext ctx;
    ctx.task_id = "fail_task";
    
    auto result = executor_->execute_task(task, ctx);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "intentional failure");
}

TEST_F(TaskExecutorTest, ExecuteBatchTasks) {
    std::vector<TaskPtr> tasks;
    for (int i = 0; i < 5; ++i) {
        auto task = TaskFactory::create_function_task("task_" + std::to_string(i), 
            [i](const TaskContext& /*ctx*/) {
                return TaskResult::ok(i);
            });
        tasks.push_back(task);
    }
    
    TaskContext ctx;
    auto results = executor_->execute_batch(tasks, ctx);
    
    EXPECT_EQ(results.size(), 5);
    for (const auto& result : results) {
        EXPECT_TRUE(result.success);
    }
}

TEST_F(TaskExecutorTest, ExecuteWithRetry) {
    int attempt_count = 0;
    auto task = TaskFactory::create_function_task("retry_task", 
        [&attempt_count](const TaskContext& /*ctx*/) {
            attempt_count++;
            if (attempt_count < 3) {
                return TaskResult::error("not yet");
            }
            return TaskResult::ok();
        });
    
    TaskContext ctx;
    RetryPolicy policy;
    policy.max_retries = 3;
    policy.retry_delay_ms = 10;
    
    auto result = executor_->execute_task_with_retry(task, ctx, policy);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(attempt_count, 3);
}

// ==================== WorkflowBuilder 测试 ====================
class WorkflowBuilderTest : public ::testing::Test {
protected:
    WorkflowBuilder builder_;
};

TEST_F(WorkflowBuilderTest, BuildSimpleWorkflow) {
    builder_.add_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_dependency("task1", "task2");
    
    auto dag = builder_.build();
    EXPECT_EQ(dag.size(), 2);
    EXPECT_FALSE(dag.has_cycle());
}

TEST_F(WorkflowBuilderTest, BuildComplexWorkflow) {
    // 构建一个复杂的工作流：
    // task1 -> task2 -> task4
    //      \-> task3 /
    
    builder_.add_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_task("task3", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_task("task4", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder_.add_dependency("task1", "task2");
    builder_.add_dependency("task1", "task3");
    builder_.add_dependency("task2", "task4");
    builder_.add_dependency("task3", "task4");
    
    auto dag = builder_.build();
    EXPECT_EQ(dag.size(), 4);
    EXPECT_FALSE(dag.has_cycle());
}

TEST_F(WorkflowBuilderTest, AddTaskWithAgent) {
    builder_.add_task("agent_task", "coder", "write a function", 600);
    
    auto dag = builder_.build();
    EXPECT_EQ(dag.size(), 1);
}

// ==================== WorkflowRunner 测试 ====================
class WorkflowRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto thread_pool = std::make_shared<ThreadPool>();
        runner_ = std::make_shared<WorkflowRunner>(thread_pool);
    }
    
    std::shared_ptr<WorkflowRunner> runner_;
};

TEST_F(WorkflowRunnerTest, RunEmptyWorkflow) {
    WorkflowBuilder builder;
    auto dag = builder.build();
    
    auto result = runner_->run(dag);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.status, WorkflowStatus::SUCCESS);
}

TEST_F(WorkflowRunnerTest, RunSimpleWorkflow) {
    WorkflowBuilder builder;
    
    builder.add_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok(std::string("result1"));
    });
    
    builder.add_task("task2", [](const TaskContext& ctx) {
        // 获取上游结果
        auto upstream = ctx.get_upstream_result<std::string>("task1");
        if (upstream) {
            return TaskResult::ok(*upstream + "_result2");
        }
        return TaskResult::error("no upstream result");
    });
    
    builder.add_dependency("task1", "task2");
    
    auto dag = builder.build();
    auto result = runner_->run(dag);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.completed_tasks, 2);
}

TEST_F(WorkflowRunnerTest, RunParallelTasks) {
    WorkflowBuilder builder;
    
    // 两个独立的任务可以并行执行
    builder.add_task("task1", [](const TaskContext& /*ctx*/) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TaskResult::ok();
    });
    
    builder.add_task("task2", [](const TaskContext& /*ctx*/) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return TaskResult::ok();
    });
    
    auto dag = builder.build();
    
    auto start = std::chrono::steady_clock::now();
    auto result = runner_->run(dag);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    
    EXPECT_TRUE(result.success);
    // 并行执行应该小于 250ms（两个 100ms 任务串行应该是 200ms+）
    EXPECT_LT(elapsed, 250);
}

TEST_F(WorkflowRunnerTest, HandleTaskFailure) {
    WorkflowBuilder builder;
    
    builder.add_task("task1", [](const TaskContext& /*ctx*/) {
        return TaskResult::error("task1 failed");
    });
    
    builder.add_task("task2", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok();
    });
    
    builder.add_dependency("task1", "task2");
    
    auto dag = builder.build();
    auto result = runner_->run(dag);
    
    // 默认策略是 FAIL_FAST，task1 失败后应该停止
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status, WorkflowStatus::FAILED);
}

TEST_F(WorkflowRunnerTest, GetStatus) {
    auto status = runner_->get_status();
    EXPECT_FALSE(status.running);
}

// ==================== Integration Test ====================
TEST(WorkflowIntegrationTest, EndToEndWorkflow) {
    // 创建线程池
    auto thread_pool = std::make_shared<ThreadPool>();
    
    // 创建执行器和运行器
    auto executor = std::make_shared<TaskExecutor>(thread_pool);
    auto runner = std::make_shared<WorkflowRunner>(executor, 
        std::make_shared<MemoryStorage>());
    
    // 构建工作流
    WorkflowBuilder builder;
    
    // 任务1：生成数据
    builder.add_task("generate", [](const TaskContext& /*ctx*/) {
        return TaskResult::ok(std::vector<int>{1, 2, 3, 4, 5});
    });
    
    // 任务2：处理数据
    builder.add_task("process", [](const TaskContext& ctx) {
        auto data = ctx.get_upstream_result<std::vector<int>>("generate");
        if (data) {
            int sum = 0;
            for (int val : *data) {
                sum += val;
            }
            return TaskResult::ok(sum);
        }
        return TaskResult::error("no data");
    });
    
    // 任务3：输出结果
    builder.add_task("output", [](const TaskContext& ctx) {
        auto sum = ctx.get_upstream_result<int>("process");
        if (sum) {
            return TaskResult::ok("Sum: " + std::to_string(*sum));
        }
        return TaskResult::error("no sum");
    });
    
    builder.add_dependency("generate", "process");
    builder.add_dependency("process", "output");
    
    // 执行工作流
    auto dag = builder.build();
    auto result = runner->run(dag);
    
    // 验证结果
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.completed_tasks, 3);
    EXPECT_EQ(result.failed_tasks, 0);
}
