#include "test_framework.hpp"
#include "agent/runtime/runtime.hpp"
#include "agent/runtime/runtime_factory.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "test_util.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace {

ben_gear::workspace::WorkspaceContext make_lifecycle_ws_ctx(
    const std::filesystem::path& base_dir,
    const std::string& username = "lifecycle_user",
    const std::string& workspace = "lifecycle_workspace") {
    ben_gear::base::TierPaths tier_paths{
        base_dir / "global",
        base_dir / "users" / username,
        base_dir / "users" / username / "workspaces" / workspace
    };

    return ben_gear::workspace::WorkspaceContext{
        std::move(tier_paths),
        workspace,
        base_dir.string(),
        username,
        std::string("lifecycle-session")
    };
}

ben_gear::config::Settings make_lifecycle_settings(const std::filesystem::path& project_dir) {
    ben_gear::config::Settings settings;
    settings.workspace = project_dir;
    settings.agent.max_tool_steps = 1;
    settings.agent.max_tool_calls = 1;
    settings.agent.max_tool_calls_per_step = 1;
    settings.mcp_servers.clear();
    return settings;
}

}  // namespace

class LifecycleTest : public bengear::test::TmpDirTest {};

TEST_F(LifecycleTest, RuntimeLightConstructionLifetime) {
    // Runtime 自身是 shared_ptr 管理的资源，构造后不泄露
    std::weak_ptr<ben_gear::agent::runtime::Runtime> weak;
    {
        auto runtime = ben_gear::agent::runtime::RuntimeFactory::create_uninitialized(
            make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = runtime;
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, RuntimeFullConstructionDoesNotCreateCycle) {
    std::weak_ptr<ben_gear::agent::runtime::Runtime> weak;
    {
        auto runtime = ben_gear::agent::runtime::RuntimeFactory::create(
            make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = runtime;
        EXPECT_FALSE(weak.expired());
        // Runtime 自身拥有 workflow_engine / sub_agent_runtime，
        // 不再有额外的 SharedResources 间接层
        EXPECT_TRUE(runtime->services().resolve<ben_gear::workflow::WorkflowEngine>() != nullptr);
        // sub_agent_runtime 在初始化后创建
        EXPECT_TRUE(runtime->services().resolve<ben_gear::agent::runtime::SubAgentRuntime>() != nullptr);
    }
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, WorkflowResourcesDoNotStronglyOwnRuntime) {
    std::weak_ptr<ben_gear::agent::runtime::Runtime> weak;
    ben_gear::workflow::WorkflowResources workflow_resources;
    {
        auto runtime = ben_gear::agent::runtime::RuntimeFactory::create(
            make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = runtime;
        auto* wf_engine = runtime->services().resolve<ben_gear::workflow::WorkflowEngine>();
        EXPECT_TRUE(wf_engine != nullptr);
        EXPECT_TRUE(workflow_resources.lifetime_context == nullptr);
    }
    // WorkflowResources 只持有非 owning 指针 + weak bound callables,
    // 不会保持 Runtime 存活
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, SubAgentRuntimeDoesNotStronglyOwnRuntime) {
    std::weak_ptr<ben_gear::agent::runtime::Runtime> weak;
    {
        auto runtime = ben_gear::agent::runtime::RuntimeFactory::create(
            make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = runtime;
        auto* sub = runtime->services().resolve<ben_gear::agent::runtime::SubAgentRuntime>();
        EXPECT_NE(sub, nullptr);
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}
