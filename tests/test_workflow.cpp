#include <gtest/gtest.h>
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
        executor_ = std::make_shared<TaskExecutor>();
    }
    
    std::shared_ptr<TaskExecutor> executor_;
    std::shared_ptr<ThreadPool> thread_pool_;
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


// ==================== 命名空间隔离测试 ====================
#include "ben_gear/workflow/workflow_engine.hpp"

class WorkflowNamespaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_shared<WorkflowEngine>(nullptr);
    }
    std::shared_ptr<WorkflowEngine> engine_;
};

TEST_F(WorkflowNamespaceTest, RegisterWithNamespace_NoConflict) {
    // 同一个 workflow_id 在不同命名空间下不冲突
    WorkflowDefinition wf;
    wf.id = "weather-compare";
    wf.name = "Weather Compare";

    auto id_a = engine_->register_workflow(wf, "user1::default::sess_abc");
    auto id_b = engine_->register_workflow(wf, "user1::default::sess_def");

    EXPECT_EQ(id_a, "user1::default::sess_abc::weather-compare");
    EXPECT_EQ(id_b, "user1::default::sess_def::weather-compare");

    // 两个都能查到
    EXPECT_TRUE(engine_->get_workflow(id_a).has_value());
    EXPECT_TRUE(engine_->get_workflow(id_b).has_value());
}

TEST_F(WorkflowNamespaceTest, RegisterWithoutNamespace) {
    WorkflowDefinition wf;
    wf.id = "simple-wf";
    wf.name = "Simple";

    auto id = engine_->register_workflow(wf);
    EXPECT_EQ(id, "simple-wf");
    EXPECT_TRUE(engine_->get_workflow(id).has_value());
}

TEST_F(WorkflowNamespaceTest, ListWorkflowsByNamespace) {
    WorkflowDefinition wf1, wf2, wf3;
    wf1.id = "wf-a"; wf1.name = "A";
    wf2.id = "wf-b"; wf2.name = "B";
    wf3.id = "wf-c"; wf3.name = "C";

    engine_->register_workflow(wf1, "ns1");
    engine_->register_workflow(wf2, "ns1");
    engine_->register_workflow(wf3, "ns2");

    auto ns1_list = engine_->list_workflows("ns1");
    auto ns2_list = engine_->list_workflows("ns2");

    EXPECT_EQ(ns1_list.size(), 2u);
    EXPECT_EQ(ns2_list.size(), 1u);
}

TEST_F(WorkflowNamespaceTest, SameNamespaceOverwrites) {
    WorkflowDefinition wf1, wf2;
    wf1.id = "my-wf"; wf1.name = "V1";
    wf2.id = "my-wf"; wf2.name = "V2";

    engine_->register_workflow(wf1, "ns1");
    engine_->register_workflow(wf2, "ns1");

    auto list = engine_->list_workflows("ns1");
    EXPECT_EQ(list.size(), 1u);  // 覆盖，不是新增
    EXPECT_EQ(engine_->get_workflow("ns1::my-wf")->name, "V2");
}

// ==================== thread_local 命名空间自动隔离测试 ====================

TEST(WorkflowThreadLocalNamespace, AutoNamespaceFromContext) {
    auto engine = std::make_shared<WorkflowEngine>(nullptr);

    // 模拟 Agent 设置命名空间
    WorkflowEngine::set_current_namespace("user1::workspace::sess_abc");

    WorkflowDefinition wf;
    wf.id = "my-wf";
    wf.name = "Test";

    // register_workflow 不传 ns，自动使用 current_namespace
    auto id = engine->register_workflow(wf);
    EXPECT_EQ(id, "user1::workspace::sess_abc::my-wf");
    EXPECT_TRUE(engine->get_workflow(id).has_value());

    WorkflowEngine::clear_current_namespace();
}

TEST(WorkflowThreadLocalNamespace, ExplicitNsOverridesContext) {
    auto engine = std::make_shared<WorkflowEngine>(nullptr);

    WorkflowEngine::set_current_namespace("auto_ns");
    WorkflowDefinition wf;
    wf.id = "my-wf";

    // 显式 ns 优先于 current_namespace
    auto id = engine->register_workflow(wf, "explicit_ns");
    EXPECT_EQ(id, "explicit_ns::my-wf");

    WorkflowEngine::clear_current_namespace();
}

TEST(WorkflowThreadLocalNamespace, NoNamespaceNoContext) {
    auto engine = std::make_shared<WorkflowEngine>(nullptr);

    // 无 current_namespace，也无显式 ns
    WorkflowDefinition wf;
    wf.id = "raw-wf";
    auto id = engine->register_workflow(wf);
    EXPECT_EQ(id, "raw-wf");
}

TEST(WorkflowThreadLocalNamespace, DifferentSessionsIsolated) {
    auto engine = std::make_shared<WorkflowEngine>(nullptr);

    WorkflowDefinition wf;
    wf.id = "shared-wf";

    // 模拟会话 A
    WorkflowEngine::set_current_namespace("sess_A");
    auto id_a = engine->register_workflow(wf);

    // 模拟会话 B
    WorkflowEngine::set_current_namespace("sess_B");
    auto id_b = engine->register_workflow(wf);

    WorkflowEngine::clear_current_namespace();

    EXPECT_NE(id_a, id_b);
    EXPECT_EQ(id_a, "sess_A::shared-wf");
    EXPECT_EQ(id_b, "sess_B::shared-wf");
    EXPECT_TRUE(engine->get_workflow(id_a).has_value());
    EXPECT_TRUE(engine->get_workflow(id_b).has_value());
}
