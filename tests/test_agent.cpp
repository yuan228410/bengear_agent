#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/agent/agent_impl.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/workspace/manager.hpp"
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
        ben_gear::base::container::String(workspace.c_str()),
        ben_gear::base::container::String(username.c_str()),
        ben_gear::base::container::String()  // 空 session_id
    };
}

}  // namespace

// ==================== AgentImpl 单元测试 ====================

class AgentImplTest : public TmpDirTest {};

TEST_F(AgentImplTest, BuildSystemPrompt_DefaultPrompt) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    
    auto prompt = ben_gear::agent::AgentImpl::build_system_prompt(*resources);
    
    EXPECT_FALSE(prompt.empty());
    EXPECT_NE(prompt.find("BenGear"), std::string::npos);
}

TEST_F(AgentImplTest, BuildSystemPrompt_CustomPrompt) {
    ben_gear::config::Settings settings;
    settings.agent.system_prompt = "Custom system prompt";
    
    auto ws_ctx = make_test_ws_ctx(dir());
    
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    
    auto prompt = ben_gear::agent::AgentImpl::build_system_prompt(*resources);
    
    EXPECT_NE(prompt.find("Custom system prompt"), std::string::npos);
}

TEST_F(AgentImplTest, ExtractResponseText_OpenAI) {
    ben_gear::Json response = {
        {"choices", {
            {{"message", {
                {"content", "Hello, world!"}
            }}}
        }}
    };
    
    auto text = ben_gear::agent::AgentImpl::extract_response_text(
        response, ben_gear::config::Provider::openai);
    
    EXPECT_EQ(text, "Hello, world!");
}

TEST_F(AgentImplTest, ExtractResponseText_Anthropic) {
    ben_gear::Json response = {
        {"content", {
            {{"type", "text"}, {"text", "Hello from Anthropic!"}}
        }}
    };
    
    auto text = ben_gear::agent::AgentImpl::extract_response_text(
        response, ben_gear::config::Provider::anthropic);
    
    EXPECT_EQ(text, "Hello from Anthropic!");
}

TEST_F(AgentImplTest, ExtractResponseText_Empty) {
    ben_gear::Json response = {};
    
    auto text_openai = ben_gear::agent::AgentImpl::extract_response_text(
        response, ben_gear::config::Provider::openai);
    EXPECT_TRUE(text_openai.empty());
    
    auto text_anthropic = ben_gear::agent::AgentImpl::extract_response_text(
        response, ben_gear::config::Provider::anthropic);
    EXPECT_TRUE(text_anthropic.empty());
}

// ==================== AgentCallbacks 测试 ====================

class AgentCallbacksTest : public ::testing::Test {};

TEST_F(AgentCallbacksTest, NullAgentCallbacks_NoOp) {
    ben_gear::agent::NullAgentCallbacks callbacks;
    
    // 所有回调应该安全执行，不做任何事
    EXPECT_NO_THROW(callbacks.on_token("test"));
    EXPECT_NO_THROW(callbacks.on_thinking("thinking"));
    EXPECT_NO_THROW(callbacks.on_tool_call({}));
    EXPECT_NO_THROW(callbacks.on_tool_result({}));
}

TEST_F(AgentCallbacksTest, CustomCallbacks_Invoked) {
    std::vector<std::string> tokens;
    
    class TestCallbacks : public ben_gear::agent::AgentCallbacks {
    public:
        std::vector<std::string>& tokens_;
        TestCallbacks(std::vector<std::string>& tokens) : tokens_(tokens) {}
        
        void on_token(std::string_view token) const override {
            tokens_.push_back(std::string(token));
        }
    };
    
    TestCallbacks callbacks(tokens);
    callbacks.on_token("Hello");
    callbacks.on_token(" ");
    callbacks.on_token("World");
    
    EXPECT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "Hello");
    EXPECT_EQ(tokens[1], " ");
    EXPECT_EQ(tokens[2], "World");
}

// ==================== Agent 构造函数测试 ====================

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

// ==================== Agent 并发测试 ====================

class AgentConcurrencyTest : public TmpDirTest {};

TEST_F(AgentConcurrencyTest, ConcurrentEnableMemoryToggle) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    
    std::atomic<int> true_count{0};
    std::atomic<int> false_count{0};
    
    // 多线程并发切换 enable_memory
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&agent, &true_count, &false_count, i]() {
            for (int j = 0; j < 100; ++j) {
                bool value = (i + j) % 2 == 0;
                agent.set_enable_memory(value);
                
                // 立即读取，验证线程安全
                bool current = agent.enable_memory();
                if (current) {
                    true_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    false_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证所有读写操作都成功完成
    EXPECT_EQ(true_count + false_count, 10 * 100);
}

TEST_F(AgentConcurrencyTest, ConcurrentResourceAccess) {
    ben_gear::config::Settings settings;
    settings.model = "gpt-4";
    
    auto ws_ctx = make_test_ws_ctx(dir());
    
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    
    std::atomic<int> success_count{0};
    
    // 多线程并发访问共享资源
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&agent, &success_count]() {
            for (int j = 0; j < 100; ++j) {
                // 并发访问各种资源
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
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), 8 * 100);
}

// ==================== Agent 资源管理测试 ====================

class AgentResourceTest : public TmpDirTest {};

TEST_F(AgentResourceTest, SharedResourcesLifetime) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    
    // 创建多个 Agent 共享同一资源
    std::vector<std::unique_ptr<ben_gear::agent::Agent>> agents;
    for (int i = 0; i < 5; ++i) {
        agents.push_back(std::make_unique<ben_gear::agent::Agent>(resources));
    }
    
    // 验证所有 Agent 都持有相同的资源
    for (const auto& agent : agents) {
        EXPECT_EQ(agent->resources(), resources);
    }
    
    // 销毁部分 Agent
    agents.erase(agents.begin(), agents.begin() + 3);
    
    // 验证资源仍然有效
    EXPECT_TRUE(resources != nullptr);
    for (const auto& agent : agents) {
        EXPECT_EQ(agent->resources(), resources);
    }
}

TEST_F(AgentResourceTest, RegisterCustomTool) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    ben_gear::agent::Agent agent(std::move(settings), std::move(ws_ctx));
    
    // 注册自定义工具
    ben_gear::base::container::Vector<std::pair<ben_gear::base::container::String, ben_gear::llm::ToolParameterSchema>> params;
    params.push_back({
        ben_gear::base::container::String("input"),
        ben_gear::llm::ToolParameterSchema{
            ben_gear::base::container::String("string"),
            ben_gear::base::container::String("Input text")
        }
    });
    
    agent.register_tool(
        ben_gear::base::container::String("custom_tool"),
        ben_gear::base::container::String("A custom tool for testing"),
        params,
        [](const ben_gear::Json& args) -> ben_gear::base::container::String {
            return ben_gear::base::container::String("custom_result");
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

class AgentPerformanceTest : public TmpDirTest {};

TEST_F(AgentPerformanceTest, SystemPromptBuild_Performance) {
    ben_gear::config::Settings settings;
    settings.agent.system_prompt = "This is a test system prompt that should be reasonably long.";
    
    auto ws_ctx = make_test_ws_ctx(dir());
    
    auto resources = std::make_shared<ben_gear::agent::SharedResources>(
        std::move(settings), std::move(ws_ctx));
    
    // 测试系统提示构建性能
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        auto prompt = ben_gear::agent::AgentImpl::build_system_prompt(*resources);
        EXPECT_FALSE(prompt.empty());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 10000 次构建应该在 100ms 内完成（每次 < 0.01ms）
    EXPECT_LT(duration.count(), 100);
}

TEST_F(AgentPerformanceTest, ExtractResponseText_Performance) {
    ben_gear::Json response = {
        {"choices", {
            {{"message", {
                {"content", "This is a test response with some content."}
            }}}
        }}
    };
    
    // 测试响应文本提取性能
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100000; ++i) {
        auto text = ben_gear::agent::AgentImpl::extract_response_text(
            response, ben_gear::config::Provider::openai);
        EXPECT_FALSE(text.empty());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 100000 次提取应该在 500ms 内完成（每次 < 0.005ms）
    EXPECT_LT(duration.count(), 500);
}
