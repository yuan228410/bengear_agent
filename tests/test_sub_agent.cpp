#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/agent/sub_agent.hpp"
#include "ben_gear/agent/sub_agent_config.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/utils/json.hpp"

using Json = ben_gear::Json;

using namespace ben_gear::agent;
using namespace ben_gear::llm;

// ==================== SubAgentEvent 工厂方法测试 ====================

TEST(SubAgentEventTest, MakeStarted) {
    auto e = SubAgentEvent::make_started("task1", "hello world", 1, 3);
    EXPECT_EQ(std::string(e.task_id.data(), e.task_id.size()), "task1");
    EXPECT_EQ(e.type, SubAgentEventType::started);
    auto* data = std::get_if<SubAgentStartedData>(&e.payload);
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(std::string(data->prompt_summary.data(), data->prompt_summary.size()), "hello world");
    EXPECT_EQ(data->index, 1);
    EXPECT_EQ(data->total, 3);
}

TEST(SubAgentEventTest, MakeToolCall) {
    ToolCallRequest call;
    call.id = "call1";
    call.name = "read_file";
    auto e = SubAgentEvent::make_tool_call("task1", call);
    EXPECT_EQ(e.type, SubAgentEventType::tool_call);
    auto* c = std::get_if<ToolCallRequest>(&e.payload);
    EXPECT_TRUE(c != nullptr);
    EXPECT_EQ(std::string(c->name.data(), c->name.size()), "read_file");
}

TEST(SubAgentEventTest, MakeToolResult) {
    ToolCallResult result;
    result.name = "read_file";
    result.success = true;
    result.output = "file content";
    auto e = SubAgentEvent::make_tool_result("task1", result);
    EXPECT_EQ(e.type, SubAgentEventType::tool_result);
    auto* r = std::get_if<ToolCallResult>(&e.payload);
    EXPECT_TRUE(r != nullptr);
    EXPECT_TRUE(r->success);
}

TEST(SubAgentEventTest, MakeToken) {
    auto e = SubAgentEvent::make_token("task1", ben_gear::base::container::String("hello"));
    EXPECT_EQ(e.type, SubAgentEventType::token_output);
    auto* data = std::get_if<SubAgentTokenData>(&e.payload);
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(std::string(data->token.data(), data->token.size()), "hello");
}

TEST(SubAgentEventTest, MakeCompleted) {
    auto e = SubAgentEvent::make_completed("task1", ben_gear::base::container::String("done"),
                                            TokenUsage{100, 50, 150, 0}, 1.5, 2);
    EXPECT_EQ(e.type, SubAgentEventType::completed);
    auto* data = std::get_if<SubAgentCompletedData>(&e.payload);
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(std::string(data->output_summary.data(), data->output_summary.size()), "done");
    EXPECT_EQ(data->usage.total_tokens, 150);
    EXPECT_DOUBLE_EQ(data->elapsed_seconds, 1.5);
    EXPECT_EQ(data->tool_steps, 2);
}

TEST(SubAgentEventTest, MakeFailed) {
    auto e = SubAgentEvent::make_failed("task1", ben_gear::base::container::String("error msg"));
    EXPECT_EQ(e.type, SubAgentEventType::failed);
    auto* data = std::get_if<SubAgentFailedData>(&e.payload);
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(std::string(data->error.data(), data->error.size()), "error msg");
}

TEST(SubAgentEventTest, MakeCancelled) {
    auto e = SubAgentEvent::make_cancelled("task1");
    EXPECT_EQ(e.type, SubAgentEventType::cancelled);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(e.payload));
}

TEST(SubAgentEventTest, MakeTimeout) {
    auto e = SubAgentEvent::make_timeout("task1");
    EXPECT_EQ(e.type, SubAgentEventType::timeout);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(e.payload));
}

// ==================== SubAgentConfig 默认值测试 ====================

TEST(SubAgentConfigTest, Defaults) {
    SubAgentConfig cfg;
    EXPECT_EQ(cfg.max_parallel, 5);
    EXPECT_EQ(cfg.default_max_steps, 20);
    EXPECT_EQ(cfg.default_timeout.count(), 120000);
    EXPECT_TRUE(cfg.auto_summary);
    EXPECT_EQ(cfg.max_output_chars, 4000);
    EXPECT_TRUE(cfg.tool_filter_default.empty());
    EXPECT_TRUE(cfg.model_override.empty());
    EXPECT_EQ(cfg.context_length_override, 0);
    EXPECT_TRUE(cfg.aggregate_parallel);
}

// ==================== SessionType 枚举测试 ====================

TEST(SessionTypeTest, Values) {
    EXPECT_EQ(static_cast<int>(SessionType::main), 0);
    EXPECT_EQ(static_cast<int>(SessionType::sub_agent), 1);
    EXPECT_EQ(static_cast<int>(SessionType::workflow), 2);
}

// ==================== SubAgentStatus 枚举测试 ====================

TEST(SubAgentStatusTest, Values) {
    EXPECT_EQ(static_cast<int>(SubAgentStatus::pending), 0);
    EXPECT_EQ(static_cast<int>(SubAgentStatus::running), 1);
    EXPECT_EQ(static_cast<int>(SubAgentStatus::completed), 2);
    EXPECT_EQ(static_cast<int>(SubAgentStatus::failed), 3);
    EXPECT_EQ(static_cast<int>(SubAgentStatus::cancelled), 4);
    EXPECT_EQ(static_cast<int>(SubAgentStatus::timeout), 5);
}

TEST(SubAgentStatusTest, Names) {
    EXPECT_EQ(std::string(sub_agent_status_name(SubAgentStatus::completed)), "completed");
    EXPECT_EQ(std::string(sub_agent_status_name(SubAgentStatus::timeout)), "timeout");
}

// ==================== SubAgentResult 默认值测试 ====================

TEST(SubAgentResultTest, Defaults) {
    SubAgentResult r;
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.status, SubAgentStatus::pending);
    EXPECT_TRUE(r.output.empty());
    EXPECT_TRUE(r.full_output.empty());
    EXPECT_TRUE(r.error.empty());
    EXPECT_EQ(r.tool_steps, 0);
    EXPECT_FALSE(r.was_truncated);
    EXPECT_FALSE(r.was_summarized);
}

// ==================== SubAgentTask 默认值测试 ====================

TEST(SubAgentTaskTest, Defaults) {
    SubAgentTask t;
    EXPECT_TRUE(t.id.empty());
    EXPECT_TRUE(t.prompt.empty());
    EXPECT_TRUE(t.system_prompt.empty());
    EXPECT_TRUE(t.tool_filter.empty());
    EXPECT_EQ(t.max_steps, 0);
    EXPECT_EQ(t.timeout.count(), 0);
    EXPECT_TRUE(t.model_override.empty());
    EXPECT_TRUE(t.speculative_models.empty());
}

// ==================== SubAgentEvent variant 访问测试 ====================

TEST(SubAgentEventTest, VariantAccessWrongTypeReturnsNull) {
    auto e = SubAgentEvent::make_started("t1", "prompt", 1, 1);
    // started event should not contain ToolCallRequest
    auto* call = std::get_if<ToolCallRequest>(&e.payload);
    EXPECT_TRUE(call == nullptr);
    // but should contain SubAgentStartedData
    auto* data = std::get_if<SubAgentStartedData>(&e.payload);
    EXPECT_TRUE(data != nullptr);
}

TEST(SubAgentEventTest, CompletedHoldsString) {
    auto e = SubAgentEvent::make_completed("t1", ben_gear::base::container::String("result text"));
    auto* cdata = std::get_if<SubAgentCompletedData>(&e.payload);
    EXPECT_TRUE(cdata != nullptr);
    EXPECT_EQ(std::string(cdata->output_summary.data(), cdata->output_summary.size()), "result text");
    // should not hold other types
    auto* data = std::get_if<SubAgentStartedData>(&e.payload);
    EXPECT_TRUE(data == nullptr);
}

TEST(SubAgentEventTest, CancelledAndTimeoutHoldMonostate) {
    auto e1 = SubAgentEvent::make_cancelled("t1");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(e1.payload));

    auto e2 = SubAgentEvent::make_timeout("t2");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(e2.payload));
}

TEST(SubAgentEventTest, TimestampIsRecent) {
    auto before = std::chrono::steady_clock::now();
    auto e = SubAgentEvent::make_started("t1", "p", 1, 1);
    auto after = std::chrono::steady_clock::now();
    EXPECT_TRUE(e.timestamp >= before);
    EXPECT_TRUE(e.timestamp <= after);
}
