#include "ben_gear/domain/event.hpp"
#include "ben_gear/test/test_framework.hpp"

using namespace ben_gear;

TEST(DomainEventTest, TokenEventIsUiFreeStructuredPayload) {
    auto event = domain::DomainEvent::token("hello");

    EXPECT_EQ(event.source, "agent");
    EXPECT_EQ(event.type, "token");
    ASSERT_TRUE(std::holds_alternative<base::container::String>(event.payload));
    EXPECT_EQ(std::get<base::container::String>(event.payload), "hello");
}

TEST(DomainEventTest, ToolResultCarriesStatusWithoutUiFormatting) {
    llm::ToolCallResult result;
    result.tool_call_id = "call-1";
    result.name = "write_file";
    result.success = false;
    result.output = "denied";

    auto event = domain::DomainEvent::tool_result(result);

    EXPECT_EQ(event.source, "tool");
    EXPECT_EQ(event.type, "tool_result");
    EXPECT_EQ(event.entity_id, "call-1");
    EXPECT_EQ(event.status, "failed");
    EXPECT_EQ(event.message, "write_file");
    ASSERT_TRUE(std::holds_alternative<llm::ToolCallResult>(event.payload));
    EXPECT_EQ(std::get<llm::ToolCallResult>(event.payload).output, "denied");
}

TEST(DomainEventTest, UsageEventKeepsMetricsStructured) {
    llm::TokenUsage usage;
    usage.prompt_tokens = 10;
    usage.completion_tokens = 5;
    usage.total_tokens = 15;
    llm::RequestLatency latency;
    latency.total_seconds = 1.25;

    auto event = domain::DomainEvent::usage(usage, latency, "model-x", 4096);

    EXPECT_EQ(event.source, "llm");
    EXPECT_EQ(event.type, "response_stats");
    ASSERT_TRUE(std::holds_alternative<llm::TokenUsage>(event.payload));
    EXPECT_EQ(std::get<llm::TokenUsage>(event.payload).total_tokens, 15);
    EXPECT_EQ(event.field_view(domain::event_field::model), "model-x");
    EXPECT_EQ(event.field_view(domain::event_field::context_length), "4096");
}

TEST(DomainEventTest, SinkReceivesStructuredEvents) {
    struct CapturingSink final : domain::EventSink {
        mutable int count = 0;
        mutable base::container::String last_type;
        void on_event(const domain::DomainEvent& event) const override {
            ++count;
            last_type = event.type;
        }
    } sink;

    sink.on_event(domain::DomainEvent::thinking("why"));

    EXPECT_EQ(sink.count, 1);
    EXPECT_EQ(sink.last_type, "thinking");
}

TEST(DomainEventTest, FactoryAssignsMonotonicSequenceAndWallClockTimestamp) {
    auto first = domain::DomainEvent::token("a");
    auto second = domain::DomainEvent::token("b");

    EXPECT_TRUE(first.sequence > 0);
    EXPECT_TRUE(second.sequence > first.sequence);
    EXPECT_TRUE(first.timestamp_ms > 0);
    EXPECT_TRUE(second.timestamp_ms >= first.timestamp_ms);
}
