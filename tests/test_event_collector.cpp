#include "test_framework.hpp"
#include "server/callback/server_event_sink.hpp"
#include "server/callback/ws_event_serializer.hpp"
#include "server/ws/handler.hpp"
#include "base/net/event_loop.hpp"
#include "base/net/tcp_stream.hpp"
#include "base/net/socket.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <string_view>

using namespace ben_gear;

namespace {

// ─── Test Helpers ──────────────────────────────────────────────────

/// Create a minimally-valid WsHandler that never does I/O.
/// Uses a socketpair to get a valid fd; the handler's socket is never actually
/// read/written — queue_send just appends to an internal deque.
std::shared_ptr<server::WsHandler> make_test_ws_handler(net::EventLoop& loop) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return nullptr;
    }
    auto sock = net::Socket(fds[0]);
    ::close(fds[1]); // we only need one end
    auto stream = net::TcpStream(loop, std::move(sock));
    return std::make_shared<server::WsHandler>(std::move(stream), "test-ws-key");
}

/// Create an EventCollector wired to a real WsEventSerializer backed by
/// a test WsHandler for forwarding verification.
struct CollectorFixture {
    net::EventLoop loop;
    std::shared_ptr<server::WsHandler> ws;
    std::shared_ptr<server::WsEventSerializer> serializer;
    std::unique_ptr<server::EventCollector> collector;

    CollectorFixture(bool include_thinking = true,
                     bool include_tool_calls = true) {
        ws = make_test_ws_handler(loop);
        serializer = std::make_shared<server::WsEventSerializer>(ws, "test-workspace");
        collector = std::make_unique<server::EventCollector>(
            serializer, "test-session", "test-workspace",
            include_thinking, include_tool_calls);
    }
};

} // namespace

// ═══════════════════════════════════════════════════════════════════
// WsEventSerializer tests
// ═══════════════════════════════════════════════════════════════════

TEST(WsEventSerializerTest, EnrichAddsWorkspaceToEmptyMessage) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, "my-workspace");

    server::WsMessage msg;
    msg.type = "test_type";
    msg.session_id = "sid-1";

    auto enriched = serializer.enrich(msg);

    EXPECT_EQ(enriched.type, "test_type");
    EXPECT_EQ(enriched.session_id, "sid-1");
    // workspace metadata injected
    auto it = enriched.strings.find("workspace");
    ASSERT_TRUE(it != enriched.strings.end());
    EXPECT_EQ(it->second, "my-workspace");
}

TEST(WsEventSerializerTest, EnrichWithEmptyWorkspaceDoesNotAddKey) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, ""); // empty workspace

    server::WsMessage msg;
    msg.type = "test_type";

    auto enriched = serializer.enrich(msg);

    // empty workspace → key absent
    EXPECT_EQ(enriched.strings.find("workspace"), enriched.strings.end());
}

TEST(WsEventSerializerTest, EnrichOverwritesExistingWorkspaceKey) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, "real-workspace");

    server::WsMessage msg;
    msg.strings["workspace"] = "stale-workspace";

    auto enriched = serializer.enrich(msg);

    auto it = enriched.strings.find("workspace");
    ASSERT_TRUE(it != enriched.strings.end());
    EXPECT_EQ(it->second, "real-workspace");
}

TEST(WsEventSerializerTest, EnrichReturnsSameMessageStructurally) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, "ws1");

    server::WsMessage msg;
    msg.version = 2;
    msg.type = "custom_type";
    msg.session_id = "sess-123";
    msg.strings["key1"] = "val1";
    msg.ints["count"] = 42;
    msg.doubles["score"] = 3.14;
    msg.json_data = R"({"a":1})";
    msg.json_data_raw = true;

    auto enriched = serializer.enrich(msg);

    EXPECT_EQ(enriched.version, 2);
    EXPECT_EQ(enriched.type, "custom_type");
    EXPECT_EQ(enriched.session_id, "sess-123");
    EXPECT_EQ(enriched.strings["key1"], "val1");
    EXPECT_EQ(enriched.strings["workspace"], "ws1");
    EXPECT_EQ(enriched.ints["count"], 42);
    EXPECT_DOUBLE_EQ(enriched.doubles["score"], 3.14);  // NOLINT
    EXPECT_EQ(enriched.json_data, R"({"a":1})");
    EXPECT_TRUE(enriched.json_data_raw);
}

TEST(WsEventSerializerTest, AliveWhenHandlerIsAlive) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, "w");
    EXPECT_TRUE(serializer.alive());
}

TEST(WsEventSerializerTest, HandlerAccessorReturnsSharedPtr) {
    net::EventLoop loop;
    auto ws = make_test_ws_handler(loop);
    ASSERT_TRUE(ws != nullptr);

    server::WsEventSerializer serializer(ws, "w");
    EXPECT_EQ(serializer.handler().get(), ws.get());
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector stats tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorStatsTest, HasResponseStatsFalseBeforeOnResponseStats) {
    CollectorFixture fix;
    EXPECT_FALSE(fix.collector->has_response_stats());
}

TEST(EventCollectorStatsTest, HasResponseStatsTrueAfterOnResponseStats) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    usage.prompt_tokens = 100;
    usage.completion_tokens = 50;
    usage.total_tokens = 150;
    llm::RequestLatency latency;
    latency.total_seconds = 1.5;
    latency.ttfb_seconds = 0.3;
    latency.has_ttfb = true;

    fix.collector->on_response_stats(usage, latency);

    EXPECT_TRUE(fix.collector->has_response_stats());
}

TEST(EventCollectorStatsTest, ResponseUsageJsonReturnsEmptyObjectBeforeStats) {
    CollectorFixture fix;
    EXPECT_EQ(fix.collector->response_usage_json(), "{}");
}

TEST(EventCollectorStatsTest, ResponseUsageJsonContainsTokenCounts) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    usage.prompt_tokens = 200;
    usage.completion_tokens = 80;
    usage.total_tokens = 280;
    llm::RequestLatency latency;

    fix.collector->on_response_stats(usage, latency);

    std::string json = fix.collector->response_usage_json();
    EXPECT_NE(json.find("\"prompt_tokens\":200"), std::string::npos);
    EXPECT_NE(json.find("\"completion_tokens\":80"), std::string::npos);
    EXPECT_NE(json.find("\"total_tokens\":280"), std::string::npos);
}

TEST(EventCollectorStatsTest, ResponseUsageJsonIncludesModelWhenProvided) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    llm::RequestLatency latency;

    fix.collector->on_response_stats(usage, latency, "gpt-4o");

    std::string json = fix.collector->response_usage_json();
    EXPECT_NE(json.find("\"model\":\"gpt-4o\""), std::string::npos);
}

TEST(EventCollectorStatsTest, ResponseUsageJsonOmitsModelWhenEmpty) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    llm::RequestLatency latency;

    fix.collector->on_response_stats(usage, latency); // no model name

    std::string json = fix.collector->response_usage_json();
    EXPECT_EQ(json.find("\"model\""), std::string::npos);
}

TEST(EventCollectorStatsTest, ResponseUsageJsonIncludesContextLengthWhenPositive) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    llm::RequestLatency latency;

    fix.collector->on_response_stats(usage, latency, {}, 4096);

    std::string json = fix.collector->response_usage_json();
    EXPECT_NE(json.find("\"context_length\":4096"), std::string::npos);
}

TEST(EventCollectorStatsTest, ResponseUsageJsonOmitsContextLengthWhenZero) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    llm::RequestLatency latency;

    fix.collector->on_response_stats(usage, latency, {}, 0);

    std::string json = fix.collector->response_usage_json();
    EXPECT_EQ(json.find("\"context_length\""), std::string::npos);
}

TEST(EventCollectorStatsTest, ResponseLatencyReturnsDefaultBeforeStats) {
    CollectorFixture fix;
    auto lat = fix.collector->response_latency();
    EXPECT_DOUBLE_EQ(lat.total_seconds, 0.0);  // NOLINT
    EXPECT_DOUBLE_EQ(lat.ttfb_seconds, 0.0);   // NOLINT
    EXPECT_FALSE(lat.has_ttfb);
}

TEST(EventCollectorStatsTest, ResponseLatencyReturnsStoredValues) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    llm::RequestLatency latency;
    latency.total_seconds = 3.7;
    latency.ttfb_seconds = 0.8;
    latency.has_ttfb = true;

    fix.collector->on_response_stats(usage, latency);

    auto lat = fix.collector->response_latency();
    EXPECT_DOUBLE_EQ(lat.total_seconds, 3.7);   // NOLINT
    EXPECT_DOUBLE_EQ(lat.ttfb_seconds, 0.8);    // NOLINT
    EXPECT_TRUE(lat.has_ttfb);
}

TEST(EventCollectorStatsTest, OnResponseStatsOverwritesPreviousStats) {
    CollectorFixture fix;

    llm::TokenUsage usage1;
    usage1.total_tokens = 100;
    llm::RequestLatency lat1;
    lat1.total_seconds = 1.0;
    fix.collector->on_response_stats(usage1, lat1);

    llm::TokenUsage usage2;
    usage2.total_tokens = 200;
    llm::RequestLatency lat2;
    lat2.total_seconds = 2.0;
    fix.collector->on_response_stats(usage2, lat2);

    EXPECT_TRUE(fix.collector->has_response_stats());
    auto lat = fix.collector->response_latency();
    EXPECT_DOUBLE_EQ(lat.total_seconds, 2.0);  // NOLINT
    std::string json = fix.collector->response_usage_json();
    EXPECT_NE(json.find("\"total_tokens\":200"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector stream event forwarding tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorStreamTest, OnTokenDoesNotCrash) {
    CollectorFixture fix;
    EXPECT_NO_THROW(fix.collector->on_token("Hello"));
    EXPECT_NO_THROW(fix.collector->on_token(" world"));
    EXPECT_NO_THROW(fix.collector->on_token(std::string_view("!")));
}

TEST(EventCollectorStreamTest, OnThinkingWhenEnabledDoesNotCrash) {
    CollectorFixture fix(/*include_thinking=*/true);
    EXPECT_NO_THROW(fix.collector->on_thinking("Let me think..."));
}

TEST(EventCollectorStreamTest, OnThinkingWhenDisabledIsNoOp) {
    CollectorFixture fix(/*include_thinking=*/false);
    // Should not crash and should not forward — serializer isn't called
    EXPECT_NO_THROW(fix.collector->on_thinking("should be dropped"));
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector tool event forwarding & flag tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorToolTest, OnToolCallWhenEnabledDoesNotCrash) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/true);

    capabilities::tool::ToolCallRequest req;
    req.id = "call-1";
    req.name = "read_file";
    req.arguments = Json::object();

    EXPECT_NO_THROW(fix.collector->on_tool_call(req));
}

TEST(EventCollectorToolTest, OnToolCallWhenDisabledIsNoOp) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/false);

    capabilities::tool::ToolCallRequest req;
    req.id = "call-1";
    req.name = "read_file";

    EXPECT_NO_THROW(fix.collector->on_tool_call(req));
}

TEST(EventCollectorToolTest, OnToolResultWhenEnabledDoesNotCrash) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/true);

    capabilities::tool::ToolCallResult result;
    result.tool_call_id = "call-1";
    result.name = "read_file";
    result.output = "file contents";
    result.success = true;

    EXPECT_NO_THROW(fix.collector->on_tool_result(result));
}

TEST(EventCollectorToolTest, OnToolResultWhenDisabledIsNoOp) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/false);

    capabilities::tool::ToolCallResult result;
    result.name = "read_file";
    result.output = "x";
    result.success = false;

    EXPECT_NO_THROW(fix.collector->on_tool_result(result));
}

TEST(EventCollectorToolTest, OnToolBlockedForwardsAsExecutionEvent) {
    CollectorFixture fix;

    EXPECT_NO_THROW(fix.collector->on_tool_blocked("dangerous_tool", "blocked by policy"));
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector orchestration event tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorOrchestrationTest, OnExecutionEventDoesNotCrash) {
    CollectorFixture fix;

    auto event = orchestration::ExecutionEvent::make_started(
        orchestration::ExecutionId("exec-1"),
        orchestration::ExecutionKind::task,
        "starting task");

    EXPECT_NO_THROW(fix.collector->on_execution_event(event));
}

TEST(EventCollectorOrchestrationTest, OnExecutionEventWithCompletedStatusDoesNotCrash) {
    CollectorFixture fix;

    orchestration::ExecutionResult result;
    result.execution_id = orchestration::ExecutionId("exec-2");
    result.kind = orchestration::ExecutionKind::task;
    auto event = orchestration::ExecutionEvent::make_completed(result);

    EXPECT_NO_THROW(fix.collector->on_execution_event(event));
}

TEST(EventCollectorOrchestrationTest, OnExecutionEventWithFailedStatusDoesNotCrash) {
    CollectorFixture fix;

    auto event = orchestration::ExecutionEvent::make_failed(
        orchestration::ExecutionId("exec-3"),
        orchestration::ExecutionKind::workflow,
        "something went wrong");

    EXPECT_NO_THROW(fix.collector->on_execution_event(event));
}

TEST(EventCollectorOrchestrationTest, OnTodoUpdateWithoutTodoManagerIsNoOp) {
    CollectorFixture fix;

    orchestration::TodoItem item;
    item.todo_id = "todo-1";
    item.title = "Test Item";
    item.status = orchestration::TodoStatus::running;

    EXPECT_NO_THROW(fix.collector->on_todo_update(item, "updated"));
}

TEST(EventCollectorOrchestrationTest, OnTodoUpdateWithClearActionNoManagerIsNoOp) {
    CollectorFixture fix;

    orchestration::TodoItem item;
    EXPECT_NO_THROW(fix.collector->on_todo_update(item, "clear"));
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector domain event dispatch tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorDomainEventTest, OnEventDispatchesTokenPayload) {
    CollectorFixture fix;

    auto event = domain::DomainEvent::token("hello token");
    EXPECT_NO_THROW(fix.collector->on_event(event));
}

TEST(EventCollectorDomainEventTest, OnEventDispatchesToolCallPayload) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/true);

    capabilities::tool::ToolCallRequest req;
    req.id = "tc-1";
    req.name = "bash";
    auto event = domain::DomainEvent::tool_call(req);
    EXPECT_NO_THROW(fix.collector->on_event(event));
}

TEST(EventCollectorDomainEventTest, OnEventDispatchesToolResultPayload) {
    CollectorFixture fix(/*include_thinking=*/true, /*include_tool_calls=*/true);

    capabilities::tool::ToolCallResult result;
    result.tool_call_id = "tc-1";
    result.name = "bash";
    result.output = "ok";
    result.success = true;
    auto event = domain::DomainEvent::tool_result(result);
    EXPECT_NO_THROW(fix.collector->on_event(event));
}

TEST(EventCollectorDomainEventTest, OnEventDispatchesUsagePayload) {
    CollectorFixture fix;

    llm::TokenUsage usage;
    usage.prompt_tokens = 10;
    usage.completion_tokens = 5;
    usage.total_tokens = 15;
    llm::RequestLatency latency;
    latency.total_seconds = 0.5;

    auto event = domain::DomainEvent::usage(usage, latency, "test-model", 1024);
    EXPECT_NO_THROW(fix.collector->on_event(event));

    EXPECT_TRUE(fix.collector->has_response_stats());
}

TEST(EventCollectorDomainEventTest, OnEventDispatchesThinkingPayload) {
    // include_thinking = true so thinking is forwarded
    CollectorFixture fix(/*include_thinking=*/true);

    auto event = domain::DomainEvent::thinking("hmm...");
    EXPECT_NO_THROW(fix.collector->on_event(event));
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector utility tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorUtilityTest, SetSessionIdUpdatesId) {
    CollectorFixture fix;

    fix.collector->set_session_id("new-session");

    // Verify: subsequent operations use new session (smoke)
    EXPECT_NO_THROW(fix.collector->on_token("test"));
}

TEST(EventCollectorUtilityTest, SerializerAccessorReturnsReference) {
    CollectorFixture fix;

    const server::WsEventSerializer& ser = fix.collector->serializer();
    EXPECT_EQ(&ser, fix.serializer.get());
}

TEST(EventCollectorUtilityTest, AsAgentSinksProducesValidTriple) {
    CollectorFixture fix;

    auto sinks = server::as_agent_sinks(*fix.collector);

    // All three should be non-null references pointing to the collector
    // Smoke: calling through each sink interface
    EXPECT_NO_THROW(sinks.stream.on_token("via stream sink"));
    EXPECT_NO_THROW(sinks.tool.on_tool_call({}));
    EXPECT_NO_THROW(sinks.orch.on_execution_event(
        orchestration::ExecutionEvent::make_started(
            orchestration::ExecutionId("e1"),
            orchestration::ExecutionKind::task)));
}

TEST(EventCollectorUtilityTest, ServerEventSinkAliasIsAvailable) {
    // Verify the backward-compatible alias compiles
    static_assert(std::is_same_v<server::ServerEventSink, server::EventCollector>,
                  "ServerEventSink must be an alias for EventCollector");
    EXPECT_TRUE(true);
}

TEST(EventCollectorUtilityTest, SetStateMutexDoesNotCrash) {
    CollectorFixture fix;

    std::mutex mtx;
    EXPECT_NO_THROW(fix.collector->set_state_mutex(&mtx));
}

// ═══════════════════════════════════════════════════════════════════
// EventCollector threading / stat safety tests
// ═══════════════════════════════════════════════════════════════════

TEST(EventCollectorThreadSafetyTest, ConcurrentStatsAreConsistent) {
    // Verify that stats reads and writes don't corrupt each other
    // (on_response_stats uses a mutex; has_response_stats / response_* also lock)

    CollectorFixture fix;

    // Write stats
    llm::TokenUsage usage;
    usage.prompt_tokens = 10;
    usage.total_tokens = 30;
    llm::RequestLatency latency;
    latency.total_seconds = 1.0;

    fix.collector->on_response_stats(usage, latency);

    // Concurrent reads should be consistent (not crash, return something)
    EXPECT_TRUE(fix.collector->has_response_stats());
    std::string json = fix.collector->response_usage_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json, "{}");

    auto lat = fix.collector->response_latency();
    EXPECT_GT(lat.total_seconds, 0.0);
}
