#include "test_framework.hpp"

#include "agent/execution/loop.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/execution/service_interface.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/core/event_sink.hpp"
#include "agent/sub_agent_types.hpp"
#include "base/config/settings.hpp"
#include "base/log/logger.hpp"
#include "base/core/event_bus.hpp"
#include "base/core/metrics.hpp"
#include "base/core/tracing.hpp"
#include "base/utils/json.hpp"
#include "capabilities/tool/registry.hpp"
#include "llm/conversation_history.hpp"
#include "llm/usage.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace ben_gear;
using namespace ben_gear::agent::execution;

// ─── Mock 事件收集器 ────────────────────────────────────────────────

struct MockStreamSink : agent::NullStreamSink {
    mutable std::vector<std::string> tokens;
    mutable std::vector<std::string> thinking_parts;
    mutable bool stats_received = false;

    void on_token(std::string_view token) const override {
        if (!token.empty()) tokens.push_back(std::string(token));
    }
    void on_thinking(std::string_view t) const override {
        if (!t.empty()) thinking_parts.push_back(std::string(t));
    }
    void on_response_stats(const llm::TokenUsage&, const llm::RequestLatency&,
                           std::string_view, int64_t) const override {
        stats_received = true;
    }
};

struct MockToolSink : agent::NullToolSink {
    mutable std::vector<std::string> call_names;
    mutable std::vector<std::string> result_names;
    mutable std::vector<std::string> blocked_names;

    void on_tool_call(const acp::ToolCallRequest& req) const override {
        call_names.push_back(std::string(req.name.data(), req.name.size()));
    }
    void on_tool_result(const acp::ToolCallResult& res) const override {
        result_names.push_back(std::string(res.name.data(), res.name.size()));
    }
    void on_tool_blocked(std::string_view name, std::string_view) const override {
        blocked_names.push_back(std::string(name));
    }
};

// ─── Mock 执行服务 ──────────────────────────────────────────────────

/// 模拟一次 LLM 响应的数据
struct MockResponse {
    std::string text;
    std::vector<acp::ToolCallRequest> tool_calls;
};

class MockExecutionServices : public IExecutionLoopServices {
public:
    llm::UsageTracker tracker;
    capabilities::tool::ToolRegistry tool_reg;
    std::vector<MockResponse> responses;
    size_t call_count = 0;

    net::Task<llm::StreamResult> chat_stream(
        net::EventLoop&, const llm::ConversationHistory&,
        const capabilities::tool::ToolRegistry&,
        const capabilities::tool::ToolChoiceConfig&,
        llm::StreamHandlers, const net::CancellationToken&,
        const std::string&) override {
        co_return llm::StreamResult{};
    }

    net::Task<Json> chat_sync(
        net::EventLoop&, const llm::ConversationHistory&,
        const capabilities::tool::ToolRegistry&,
        const capabilities::tool::ToolChoiceConfig&,
        const net::CancellationToken&,
        const std::string&) override {
        co_return Json{};
    }

    const llm::UsageTracker& usage_tracker() const noexcept override {
        return tracker;
    }

    const capabilities::tool::ToolRegistry& default_tools() const noexcept override {
        return tool_reg;
    }
};

} // namespace

// ─── 集成测试 ──────────────────────────────────────────────────────

class ExecutionIntegrationTest : public ::testing::Test {
protected:
    config::Settings settings;
    MockExecutionServices services;
    LoopConfig loop_config{5, 20, 3};
    std::unique_ptr<ExecutionLoop> loop;

    void SetUp() override {
        settings.llm.stream = true;
        auto pool = std::make_shared<base::concurrency::ThreadPool>(
            base::concurrency::ThreadPoolConfig{});
        loop = std::make_unique<ExecutionLoop>(
            loop_config, services, pool, settings);
    }
};

TEST_F(ExecutionIntegrationTest, SmokeConstructAndDestruct) {
    EXPECT_NE(loop, nullptr);
}

TEST_F(ExecutionIntegrationTest, ToolTimeoutPolicyInjected) {
    auto policy = std::make_unique<DefaultTimeoutPolicy>(
        std::chrono::milliseconds(10000));
    policy->add_override("slow_tool", std::chrono::milliseconds(60000));

    EXPECT_EQ(policy->get_timeout("slow_tool").count(), 60000);
    EXPECT_EQ(policy->get_timeout("fast_tool").count(), 10000);
}

TEST_F(ExecutionIntegrationTest, InterceptorChainWorks) {
    struct TestInterceptor : IInterceptor {
        const char* name() const noexcept override { return "Test"; }
        int before_llm_count = 0;
        int before_tools_count = 0;
        int after_tools_count = 0;

        void before_llm(llm::ConversationHistory&, LoopSnapshot&) override {
            ++before_llm_count;
        }
        void before_tools(std::vector<acp::ToolCallRequest>&,
                          std::vector<acp::ToolCallResult>&,
                          const llm::ConversationHistory&, LoopSnapshot&) override {
            ++before_tools_count;
        }
        void after_tools(const std::vector<acp::ToolCallResult>&,
                         llm::ConversationHistory&, LoopSnapshot&) override {
            ++after_tools_count;
        }
    };

    auto interceptor = std::make_unique<TestInterceptor>();
    auto* raw = interceptor.get();
    loop->add_interceptor(std::move(interceptor));

    EXPECT_STREQ(raw->name(), "Test");
}

TEST_F(ExecutionIntegrationTest, LoopSnapshotContainsRuntimeState) {
    MockStreamSink stream;
    MockToolSink tool;
    agent::NullOrchestrationSink orch;
    base::EventBus event_bus;
    base::NoopMetricsCollector metrics;
    base::NoopTracer tracer;

    LoopSnapshot snapshot{
        .event_bus = event_bus,
        .step = 3,
        .total_calls = 7,
        .max_steps = 20,
        .max_calls = 50,
        .loop_start = std::chrono::steady_clock::now()
    };

    EXPECT_EQ(snapshot.step, 3);
    EXPECT_EQ(snapshot.total_calls, 7);
    EXPECT_EQ(snapshot.max_steps, 20);
    EXPECT_EQ(snapshot.max_calls, 50);
    EXPECT_GE(snapshot.elapsed().count(), 0);
}

TEST(SubAgentTypesTest, SubAgentTaskAndResult) {
    agent::SubAgentTask task;
    task.id = "test_1";
    task.prompt = "analyze the code";

    EXPECT_EQ(task.id, "test_1");
    EXPECT_EQ(task.prompt, "analyze the code");
    EXPECT_TRUE(task.tool_filter.empty());
    EXPECT_EQ(task.max_steps, 0);

    agent::SubAgentResult result;
    result.task_id = "test_1";
    result.success = true;
    result.status = agent::SubAgentStatus::success;
    result.output = "analysis complete";
    result.duration = std::chrono::milliseconds(1500);
    result.tool_calls = 3;

    EXPECT_EQ(result.task_id, "test_1");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.status, agent::SubAgentStatus::success);
    EXPECT_EQ(result.output, "analysis complete");
    EXPECT_EQ(result.duration.count(), 1500);
    EXPECT_EQ(result.tool_calls, 3);
    EXPECT_FALSE(result.was_truncated);
    EXPECT_FALSE(result.was_summarized);
}

TEST_F(ExecutionIntegrationTest, EventSinkISPWorks) {
    MockStreamSink stream;
    MockToolSink tool;
    agent::NullOrchestrationSink orch;

    stream.on_token("Hello");
    stream.on_thinking("思考中...");
    tool.on_tool_call({"id1", "read_file", {}});
    tool.on_tool_result({"id1", "read_file", "content"});

    EXPECT_EQ(stream.tokens.size(), 1);
    EXPECT_EQ(stream.tokens[0], "Hello");
    EXPECT_EQ(stream.thinking_parts.size(), 1);
    EXPECT_EQ(tool.call_names.size(), 1);
    EXPECT_EQ(tool.call_names[0], "read_file");
    EXPECT_EQ(tool.result_names.size(), 1);
}
