#include "test_framework.hpp"
#include "agent/runtime/runtime.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "base/config/settings.hpp"
#include "workspace/manager.hpp"
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using bengear::test::TmpDirTest;

// ==================== 测试辅助函数 ====================

namespace {

/// 创建测试用的 WorkspaceContext
ben_gear::workspace::WorkspaceContext make_test_ws_ctx(
    const std::filesystem::path& base_dir,
    const std::string& username = "test_user",
    const std::string& workspace = "test_workspace") {
    
    ben_gear::base::TierPaths tier_paths{
        base_dir / "global",
        base_dir / "users" / username,
        base_dir / "users" / username / "workspaces" / workspace
    };
    
    return ben_gear::workspace::WorkspaceContext{
        std::move(tier_paths),
        workspace,
        username,
        std::string()  // 空 session_id
    };
}

}  // namespace

// ==================== AgentEventSink 测试 ====================

class AgentEventSinkTest : public ::testing::Test {};

TEST_F(AgentEventSinkTest, NullSinks_NoOp) {
    ben_gear::agent::NullStreamSink stream;
    ben_gear::agent::NullToolSink tool;
    ben_gear::agent::NullOrchestrationSink orch;
    EXPECT_NO_THROW(stream.on_token("test"));
    EXPECT_NO_THROW(stream.on_thinking("thinking"));
    EXPECT_NO_THROW(tool.on_tool_call({}));
    EXPECT_NO_THROW(tool.on_tool_result({}));
}

TEST_F(AgentEventSinkTest, CustomCallbacks_Invoked) {
    std::vector<std::string> tokens;
    class TestCallbacks : public ben_gear::agent::NullStreamSink {
    public:
        std::vector<std::string>& tokens_;
        TestCallbacks(std::vector<std::string>& tokens) : tokens_(tokens) {}
        void on_token(std::string_view token) const override {
            tokens_.push_back(std::string(token));
        }
    };
    TestCallbacks event_sink(tokens);
    
    event_sink.on_token("Hello");
    event_sink.on_token(" ");
    event_sink.on_token("World");
    
    EXPECT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "Hello");
    EXPECT_EQ(tokens[1], " ");
    EXPECT_EQ(tokens[2], "World");
}

// ==================== Agent 构造函数测试 ====================
// TODO: adapt - Runtime replaces Agent/SharedResources, enable_memory removed

#if 0
class AgentConstructionTest : public TmpDirTest {};

TEST_F(AgentConstructionTest, ConstructFromSharedResources) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    ben_gear::agent::Agent agent(resources);
    EXPECT_EQ(agent.resources(), resources);
    EXPECT_TRUE(agent.enable_memory());
}

TEST_F(AgentConstructionTest, ConstructFromSettingsAndContext) {
    ben_gear::config::Settings settings;
    settings.model = "gpt-4";
    auto ws_ctx = make_test_ws_ctx(dir());
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    EXPECT_EQ(agent.settings().model, "gpt-4");
    EXPECT_TRUE(agent.enable_memory());
}

TEST_F(AgentConstructionTest, SetEnableMemory) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    EXPECT_TRUE(agent.enable_memory());
    agent.set_enable_memory(false);
    EXPECT_FALSE(agent.enable_memory());
    agent.set_enable_memory(true);
    EXPECT_TRUE(agent.enable_memory());
}
#endif

// ==================== Agent 并发测试 ====================
// TODO: adapt - enable_memory/set_enable_memory removed, resources() accessor removed

#if 0
class AgentConcurrencyTest : public TmpDirTest {};

TEST_F(AgentConcurrencyTest, ConcurrentEnableMemoryToggle) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    std::atomic<int> true_count{0};
    std::atomic<int> false_count{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&agent, &true_count, &false_count, i]() {
            for (int j = 0; j < 100; ++j) {
                bool value = (i + j) % 2 == 0;
                agent.set_enable_memory(value);
                bool current = agent.enable_memory();
                if (current) true_count.fetch_add(1, std::memory_order_relaxed);
                else false_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(true_count + false_count, 10 * 100);
}

TEST_F(AgentConcurrencyTest, ConcurrentResourceAccess) {
    ben_gear::config::Settings settings;
    settings.model = "gpt-4";
    auto ws_ctx = make_test_ws_ctx(dir());
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&agent, &success_count]() {
            for (int j = 0; j < 100; ++j) {
                auto resources = agent.resources();
                EXPECT_TRUE(resources != nullptr);
                const auto& settings = agent.settings();
                EXPECT_EQ(settings.model, "gpt-4");
                const auto& tools = agent.tools();
                EXPECT_GE(tools.size(), 0u);
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(success_count.load(), 8 * 100);
}
#endif

// ==================== Agent 资源管理测试 ====================
// TODO: adapt - SharedResources/Lifetime tests need Runtime-based equivalent

#if 0
class AgentResourceTest : public TmpDirTest {};

TEST_F(AgentResourceTest, SharedResourcesLifetime) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    std::vector<std::unique_ptr<ben_gear::agent::Agent>> agents;
    for (int i = 0; i < 5; ++i) {
        agents.push_back(std::make_unique<ben_gear::agent::Agent>(resources));
    }
    for (const auto& agent : agents) {
        EXPECT_EQ(agent->resources(), resources);
    }
    agents.erase(agents.begin(), agents.begin() + 3);
    EXPECT_TRUE(resources != nullptr);
    for (const auto& agent : agents) {
        EXPECT_EQ(agent->resources(), resources);
    }
}
#endif

class AgentResourceTest : public TmpDirTest {};

TEST_F(AgentResourceTest, RegisterCustomTool) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    ben_gear::agent::runtime::Runtime agent(std::move(settings), std::move(ws_ctx));
    
    // 注册自定义工具
    std::vector<std::pair<std::string, ben_gear::capabilities::tool::ToolParameterSchema>> params;
    params.push_back({
        std::string("input"),
        ben_gear::capabilities::tool::ToolParameterSchema{
            std::string("string"),
            std::string("Input text")
        }
    });
    
    agent.register_tool(
        std::string("custom_tool"),
        std::string("A custom tool for testing"),
        params,
        [](const ben_gear::Json& args) -> std::string {
            (void)args;
            return std::string("custom_result");
        }
    );
    
    // 验证工具已注册
    const auto& tools = agent.tools();
    EXPECT_TRUE(tools.find("custom_tool").has_value());
}

// ==================== Agent 错误恢复测试 ====================

class AgentErrorRecoveryTest : public TmpDirTest {};

// 注意：InvalidPrompt 测试需要完整的 Session 构造流程，暂时跳过
// Session 构造需要 SessionDeps，包含 MemoryStore、ContextBuilder 等
// 这些测试应该在集成测试中进行

// ==================== Agent 性能测试 ====================
// TODO: adapt - AgentImpl removed, performance tests need new equivalents

#if 0
class AgentPerformanceTest : public TmpDirTest {};

TEST_F(AgentPerformanceTest, SystemPromptBuild_Performance) {
    ben_gear::config::Settings settings;
    settings.agent.system_prompt = "This is a test system prompt that should be reasonably long.";
    auto ws_ctx = make_test_ws_ctx(dir());
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto prompt = ben_gear::agent::AgentImpl::build_system_prompt(*resources);
        EXPECT_FALSE(prompt.empty());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 5000);
}

TEST_F(AgentPerformanceTest, ExtractResponseText_Performance) {
    ben_gear::Json response = {
        {"choices", {{{"message", {{"content", "This is a test response with some content."}}}}}}
    };
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        auto text = ben_gear::agent::AgentImpl::extract_response_text(
            response, ben_gear::config::Provider::openai);
        EXPECT_FALSE(text.empty());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 30000);
}
#endif
