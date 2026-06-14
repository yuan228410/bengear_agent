#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/workflow/task_types.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/tool/registry.hpp"

#include <any>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>

using namespace ben_gear::workflow;

TEST(WorkflowStatusTest, Names) {
    EXPECT_EQ(std::string(workflow_status_name(WorkflowStatus::RUNNING)), "running");
    EXPECT_EQ(std::string(workflow_status_name(WorkflowStatus::SUCCESS)), "success");
}

TEST(WorkflowEngineExtendedTest, ValidatesConcreteTaskTypes) {
    WorkflowEngine engine;
    WorkflowDefinition wf;
    wf.id = "wf-types";
    wf.name = "Types";
    wf.tasks = {
        {.id = "fn", .name = "Fn", .type = "function", .prompt = "ok"},
        {.id = "cond", .name = "Cond", .type = "condition", .prompt = "true", .depends_on = {"fn"}},
        {.id = "approval", .name = "Approval", .type = "approval", .prompt = "approve?", .depends_on = {"cond"}}
    };

    auto validation = engine.validate_workflow(wf);
    EXPECT_TRUE(validation.valid);
}

TEST(WorkflowEngineExtendedTest, RejectsUnknownTaskType) {
    WorkflowEngine engine;
    WorkflowDefinition wf;
    wf.id = "wf-bad";
    wf.name = "Bad";
    wf.tasks = {{.id = "bad", .name = "Bad", .type = "unknown", .prompt = "x"}};

    auto validation = engine.validate_workflow(wf);
    EXPECT_FALSE(validation.valid);
    EXPECT_THAT(validation.error, testing::HasSubstr("Unsupported task type"));
}

TEST(WorkflowTaskTypesTest, ConditionTaskEvaluatesUpstreamSuccess) {
    ConditionTaskConfig config;
    config.expression = "success:prepare";
    auto task = TaskFactoryEx::create_condition_task("cond", config);

    std::map<TaskId, TaskResult> upstream;
    upstream["prepare"] = TaskResult::ok(ben_gear::base::container::String("done"));
    auto ctx = TaskContext::from_map("cond", upstream);

    auto result = task->execute(ctx);
    EXPECT_TRUE(result.success);
    auto text = std::any_cast<ben_gear::base::container::String>(result.output);
    EXPECT_EQ(text, "true");
}

TEST(WorkflowEngineExtendedTest, AsyncStartReturnsExecutionHandle) {
    WorkflowEngine engine;
    WorkflowDefinition wf;
    wf.id = "wf-async";
    wf.name = "Async";
    wf.tasks = {{.id = "fn", .name = "Fn", .type = "function", .prompt = "ok"}};
    auto workflow_id = engine.register_workflow(wf);

    auto execution_id = engine.start_async(workflow_id);
    EXPECT_FALSE(execution_id.empty());

    auto state = engine.get_state(execution_id);
    EXPECT_TRUE(state.has_value());
}

TEST(WorkflowSchedulerTest, RunsIndependentReadyTasksInParallel) {
    DAG dag;
    auto active = std::make_shared<std::atomic<int>>(0);
    auto max_active = std::make_shared<std::atomic<int>>(0);
    auto make_task = [active, max_active](const TaskId& id) {
        return TaskFactory::create_function_task(id, [active, max_active](const TaskContext&) {
            int now = active->fetch_add(1) + 1;
            int previous = max_active->load();
            while (now > previous && !max_active->compare_exchange_weak(previous, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            active->fetch_sub(1);
            return TaskResult::ok(ben_gear::base::container::String("ok"));
        });
    };
    dag.add_task("a", make_task("a"));
    dag.add_task("b", make_task("b"));

    RetryPolicy retry_policy;
    retry_policy.max_retries = 1;
    retry_policy.retry_delay_ms = 1;
    WorkflowScheduler scheduler(std::move(dag), std::make_shared<TaskExecutor>(),
                                ErrorHandlingStrategy::FAIL_FAST, retry_policy);
    auto result = scheduler.run();

    EXPECT_TRUE(result.success);
    EXPECT_GE(max_active->load(), 2);
}

TEST(WorkflowSchedulerTest, StatusDoesNotReportBlockedFailedDependentsAsReady) {
    DAG dag;
    dag.add_task("fail", TaskFactory::create_function_task("fail", [](const TaskContext&) {
        return TaskResult::error("boom");
    }));
    dag.add_task("blocked", TaskFactory::create_function_task("blocked", [](const TaskContext&) {
        return TaskResult::ok(ben_gear::base::container::String("should not run"));
    }));
    dag.add_dependency("fail", "blocked");

    WorkflowScheduler scheduler(std::move(dag), std::make_shared<TaskExecutor>(), ErrorHandlingStrategy::CONTINUE);
    auto future = scheduler.run_async();
    auto result = future.get();
    auto status = scheduler.get_status();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(status.current_task, "");
}

TEST(WorkflowEngineExtendedTest, ToolTaskAcceptsToolConfigAlias) {
    WorkflowEngine engine;
    auto registry = std::make_shared<ben_gear::llm::ToolRegistry>();
    registry->register_tool(
        ben_gear::base::container::String("echo_tool"),
        ben_gear::base::container::String("Echo"),
        {},
        [](const ben_gear::Json& args) {
            return ben_gear::base::container::String(args.value("value", ""));
        });
    auto settings = std::make_shared<ben_gear::config::Settings>();
    auto io_context = std::make_shared<ben_gear::net::IoContext>("workflow-test");
    WorkflowResources resources;
    resources.tools = registry.get();
    resources.settings = settings.get();
    resources.wf_context = io_context.get();
    resources.lifetime_context = registry;
    engine.bind_resources(resources);

    WorkflowDefinition workflow;
    workflow.id = "wf-tool-alias";
    workflow.name = "Tool Alias";
    WorkflowTaskDefinition task;
    task.id = "tool";
    task.name = "Tool";
    task.type = "tool";
    task.config["tool"] = "echo_tool";
    task.config["arguments"] = ben_gear::Json{{"value", "ok"}};
    workflow.tasks.push_back(std::move(task));
    auto workflow_id = engine.register_workflow(workflow);

    auto state = engine.execute(workflow_id);

    EXPECT_EQ(state.status, WorkflowStatus::SUCCESS);
    ASSERT_TRUE(state.task_results.contains("tool"));
    EXPECT_TRUE(state.task_results["tool"].success);
    EXPECT_EQ(std::any_cast<ben_gear::base::container::String>(state.task_results["tool"].output), "ok");
}

TEST(WorkflowTaskTypesTest, SubAgentTaskPropagatesStructuredFailure) {
    auto registry = std::make_shared<ben_gear::llm::ToolRegistry>();
    registry->register_tool(
        ben_gear::base::container::String("delegate_task"),
        ben_gear::base::container::String("Delegate"),
        {},
        [](const ben_gear::Json&) {
            return ben_gear::base::container::String(R"({"success":false,"error":"timeout"})");
        });

    SubAgentTaskConfig config;
    config.prompt = "work";
    auto task = TaskFactoryEx::create_sub_agent_task("sub", registry, config);
    auto result = task->execute(TaskContext{});

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "timeout");
}
