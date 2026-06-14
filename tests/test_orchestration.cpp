#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/orchestration/serializer.hpp"

namespace orchestration = ben_gear::orchestration;
namespace container = ben_gear::base::container;

TEST(OrchestrationTest, StringEnumsUseContainerString) {
    auto status = orchestration::to_string(orchestration::ExecutionStatus::running);
    EXPECT_EQ(status, container::String("running"));

    auto kind = orchestration::to_string(orchestration::ExecutionKind::sub_agent);
    EXPECT_EQ(kind, container::String("sub_agent"));
}

TEST(OrchestrationTest, StoreTracksActiveAndCompletedExecutions) {
    orchestration::ExecutionStore store;

    orchestration::ExecutionContext ctx;
    ctx.execution_id = container::String("exec-1");
    ctx.trace_id = container::String("trace-1");

    store.start(ctx, orchestration::ExecutionKind::sub_agent);
    auto active_snapshot = store.snapshot();
    EXPECT_EQ(active_snapshot.running_count, 1u);
    EXPECT_EQ(active_snapshot.active.size(), 1u);

    orchestration::ExecutionValue output;
    output.text = container::String("done");
    auto result = orchestration::ExecutionResult::ok(
        container::String("exec-1"), orchestration::ExecutionKind::sub_agent, output);
    store.complete(result);

    auto completed_snapshot = store.snapshot();
    EXPECT_EQ(completed_snapshot.running_count, 0u);
    EXPECT_EQ(completed_snapshot.completed_count, 1u);
    EXPECT_EQ(completed_snapshot.completed.size(), 1u);
}

TEST(OrchestrationTest, SerializerProducesStructuredJson) {
    orchestration::ExecutionValue value;
    value.text = container::String("hello");
    value.fields[container::String("role")] = container::String("worker");

    auto result = orchestration::ExecutionResult::ok(
        container::String("exec-2"), orchestration::ExecutionKind::workflow, value);
    auto json = orchestration::to_json_string(result);

    EXPECT_THAT(json, testing::HasSubstr("\"execution_id\":\"exec-2\""));
    EXPECT_THAT(json, testing::HasSubstr("\"kind\":\"workflow\""));
    EXPECT_THAT(json, testing::HasSubstr("\"status\":\"succeeded\""));
    EXPECT_THAT(json, testing::HasSubstr("\"role\":\"worker\""));
}
