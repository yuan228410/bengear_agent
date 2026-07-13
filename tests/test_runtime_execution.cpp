#include "application/command_governance.hpp"
#include "application/runtime_execution.hpp"
#include "domain/errors.hpp"
#include "test_framework.hpp"

#include <string>
#include <vector>

namespace {

namespace application = ben_gear::application;
using ben_gear::Json;
using ben_gear::base::container::String;
using ben_gear::domain::AppError;
using ben_gear::domain::AppResult;

application::CommandDescriptor runtime_command(std::string_view action) {
    application::CommandDescriptor command;
    command.action = String(action.data(), action.size());
    command.username = String("alice");
    command.workspace_name = String("default");
    command.session_id = String("sid-1");
    command.project_path = String("/repo");
    command.subject = String("runtime subject");
    return command;
}

} // namespace

TEST(RuntimeExecutionKernelTest, PlansDryRunWithoutExecutingHooks) {
    auto command = runtime_command("patch.apply");
    command.risk = application::CommandRisk::workspace_write;
    command.mutates_workspace = true;
    command.affected_paths.push_back(String("src/a.cpp"));

    auto request = application::command_execution_request(command, true);
    bool executed = false;
    application::RuntimeExecutionKernel kernel(application::RuntimeExecutionHooks{
        {}, {}, {},
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            executed = true;
            return AppResult<Json>::success(Json{{"success", true}});
        },
        {}});

    auto result = kernel.execute(request);

    EXPECT_FALSE(executed);
    EXPECT_EQ(application::to_string(result.status), "planned");
    EXPECT_TRUE(result.output.value("dry_run", false));
    ASSERT_EQ(result.plan.steps.size(), static_cast<size_t>(5));
    EXPECT_EQ(application::to_string(result.plan.steps[0].kind), "validate");
    EXPECT_EQ(application::to_string(result.plan.steps[1].kind), "authorize");
    EXPECT_EQ(application::to_string(result.plan.steps[2].kind), "checkpoint");
    EXPECT_EQ(application::to_string(result.plan.steps[3].kind), "execute");
    EXPECT_EQ(application::to_string(result.plan.steps[4].kind), "audit");
    EXPECT_EQ(result.trace.size(), result.plan.steps.size());
}

TEST(RuntimeExecutionKernelTest, PermissionDeniedStopsBeforeCheckpointAndExecute) {
    auto command = runtime_command("test.run");
    command.risk = application::CommandRisk::command_execution;
    command.runs_command = true;

    std::vector<std::string> calls;
    auto kernel = application::make_runtime_execution_kernel(application::CommandGovernanceConfig{
        [&](const String&, const String&, const String&, std::string_view, const Json&) {
            calls.push_back("authorize");
            return Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm-1"}};
        },
        [&](const application::CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const String&, const String&, const String&, const String&, const String&, const Json& details) {
            calls.push_back("audit");
            EXPECT_EQ(details.value("outcome", ""), "failed");
            EXPECT_EQ(details["execution"].value("status", ""), "failed");
            return Json{{"success", true}, {"event", Json{{"event_id", "evt-1"}}}};
        }});

    auto result = kernel.execute(application::command_execution_request(command));

    EXPECT_EQ(application::to_string(result.status), "failed");
    EXPECT_EQ(result.output.value("error_type", ""), "permission_required");
    ASSERT_EQ(result.trace.size(), static_cast<size_t>(2));
    EXPECT_EQ(application::to_string(result.trace[0].kind), "validate");
    EXPECT_EQ(application::to_string(result.trace[1].kind), "authorize");
    EXPECT_EQ(application::to_string(result.trace[1].status), "failed");
    EXPECT_EQ(calls, (std::vector<std::string>{"authorize", "audit"}));
}

TEST(RuntimeExecutionKernelTest, CheckpointFailureStopsBeforeExecute) {
    auto command = runtime_command("git.restore");
    command.risk = application::CommandRisk::workspace_write;
    command.mutates_workspace = true;
    command.affected_paths.push_back(String("src/a.cpp"));

    std::vector<std::string> calls;
    application::RuntimeExecutionKernel kernel(application::RuntimeExecutionHooks{
        {},
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            calls.push_back("authorize");
            return AppResult<void>::success();
        },
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            calls.push_back("checkpoint");
            return AppResult<void>::failure(AppError::internal(String("checkpoint_failed"), String("checkpoint failed")));
        },
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            calls.push_back("execute");
            return AppResult<Json>::success(Json{{"success", true}});
        },
        [&](const application::ExecutionRequest&, const application::ExecutionResult& result) {
            calls.push_back("audit");
            EXPECT_EQ(application::to_string(result.status), "failed");
        }});

    auto result = kernel.execute(application::command_execution_request(command));

    EXPECT_EQ(application::to_string(result.status), "failed");
    EXPECT_EQ(result.output.value("error_type", ""), "checkpoint_failed");
    EXPECT_EQ(calls, (std::vector<std::string>{"authorize", "checkpoint", "audit"}));
}

TEST(RuntimeExecutionKernelTest, SuccessfulExecutionProducesTraceAndAudit) {
    auto command = runtime_command("test.run");
    command.risk = application::CommandRisk::command_execution;
    command.runs_command = true;
    command.subject = String("ctest");
    command.working_directory = String("build-dev");

    std::vector<std::string> calls;
    Json audit_details;
    auto kernel = application::make_runtime_execution_kernel(application::CommandGovernanceConfig{
        [&](const String&, const String&, const String&, std::string_view tool, const Json& args) {
            calls.push_back("authorize");
            EXPECT_EQ(std::string(tool), "run_tests");
            EXPECT_EQ(args["runtime_boundary"]["operation"].value("capability", ""), "test_loop");
            return Json{{"success", true}};
        },
        [&](const application::CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const String&, const String&, const String&, const String&, const String&, const Json& details) {
            calls.push_back("audit");
            audit_details = details;
            return Json{{"success", true}, {"event", Json{{"event_id", "evt-1"}}}};
        }});

    auto request = application::command_execution_request(command);
    application::RuntimeExecutionKernel executable(application::RuntimeExecutionHooks{
        {},
        [&](const application::ExecutionRequest& req, const application::ExecutionPlan&) {
            return application::make_runtime_execution_kernel(application::CommandGovernanceConfig{
                [&](const String&, const String&, const String&, std::string_view, const Json&) { return Json{{"success", true}}; },
                {}, [](const String&, const String&, const String&, const String&, const String&, const Json&) { return Json{{"success", true}}; }, {}}).execute(application::ExecutionRequest{req.request_id, req.command, req.boundary, true}).output.value("success", false)
                       ? AppResult<void>::success()
                       : AppResult<void>::failure(AppError::internal(String("unexpected"), String("unexpected")));
        },
        {},
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            calls.push_back("execute");
            return AppResult<Json>::success(Json{{"success", true}, {"exit_code", 0}});
        },
        [&](const application::ExecutionRequest& req, const application::ExecutionResult& result) {
            calls.push_back("audit");
            audit_details = Json{{"execution", application::to_json(result)}, {"runtime_boundary", ben_gear::core::to_json(req.boundary)}};
        }});

    auto result = executable.execute(request);

    EXPECT_EQ(application::to_string(result.status), "succeeded");
    EXPECT_TRUE(result.output.value("success", false));
    ASSERT_EQ(result.trace.size(), static_cast<size_t>(5));
    EXPECT_EQ(application::to_string(result.trace[0].kind), "validate");
    EXPECT_EQ(application::to_string(result.trace[1].kind), "authorize");
    EXPECT_EQ(application::to_string(result.trace[2].kind), "checkpoint");
    EXPECT_EQ(application::to_string(result.trace[3].kind), "execute");
    EXPECT_EQ(application::to_string(result.trace[4].kind), "audit");
    EXPECT_EQ(audit_details["execution"].value("status", ""), "succeeded");
    EXPECT_EQ(audit_details["runtime_boundary"]["operation"].value("capability", ""), "test_loop");
}

TEST(RuntimeExecutionKernelTest, ExecutionFailureStillAuditsTrace) {
    auto command = runtime_command("patch.apply");
    command.risk = application::CommandRisk::workspace_write;
    command.mutates_workspace = true;

    bool audited = false;
    application::RuntimeExecutionKernel kernel(application::RuntimeExecutionHooks{
        {},
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            return AppResult<void>::success();
        },
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            return AppResult<void>::success();
        },
        [&](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            return AppResult<Json>::failure(AppError::internal(String("command_failed"), String("command failed")));
        },
        [&](const application::ExecutionRequest&, const application::ExecutionResult& result) {
            audited = true;
            EXPECT_EQ(application::to_string(result.status), "failed");
        }});

    auto result = kernel.execute(application::command_execution_request(command));

    EXPECT_TRUE(audited);
    EXPECT_EQ(application::to_string(result.status), "failed");
    EXPECT_EQ(result.output.value("error_type", ""), "command_failed");
    EXPECT_EQ(application::to_string(result.trace.back().kind), "execute");
    EXPECT_EQ(application::to_string(result.trace.back().status), "failed");
}

TEST(RuntimeExecutionKernelTest, EmitsStructuredRuntimeEventsForPresenterAdapters) {
    auto command = runtime_command("test.run");
    command.risk = application::CommandRisk::command_execution;
    command.runs_command = true;

    std::vector<ben_gear::core::RuntimeEvent> events;
    application::RuntimeExecutionKernel kernel(application::RuntimeExecutionHooks{
        {},
        {},
        {},
        [](const application::ExecutionRequest&, const application::ExecutionPlan&) {
            return AppResult<Json>::success(Json{{"success", true}, {"exit_code", 0}});
        },
        {},
        [&](const ben_gear::core::RuntimeEvent& event) {
            events.push_back(event);
        }});

    auto result = kernel.execute(application::command_execution_request(command));

    EXPECT_EQ(application::to_string(result.status), "succeeded");
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(ben_gear::core::to_string(events.front().kind), "step_started");
    EXPECT_EQ(events.front().request_id, String("test.run"));
    bool produced_output = false;
    bool completed_audit = false;
    for (const auto& event : events) {
        if (event.kind == ben_gear::core::RuntimeEventKind::output_produced) {
            produced_output = true;
            EXPECT_TRUE(event.details["output"].value("success", false));
        }
        if (event.step_id == String("audit") && event.kind == ben_gear::core::RuntimeEventKind::step_succeeded) {
            completed_audit = true;
        }
    }
    EXPECT_TRUE(produced_output);
    EXPECT_TRUE(completed_audit);
}
