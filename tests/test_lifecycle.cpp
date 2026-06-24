#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/agent/sub_agent.hpp"
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
        ben_gear::base::container::String(workspace.c_str()),
        ben_gear::base::container::String(base_dir.string().c_str()),
        ben_gear::base::container::String(username.c_str()),
        ben_gear::base::container::String("lifecycle-session")
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

TEST_F(LifecycleTest, SharedResourcesLightConstructionDoesNotCreateOwnershipCycle) {
    std::weak_ptr<ben_gear::agent::SharedResources> weak;
    {
        auto resources = std::make_shared<ben_gear::agent::SharedResources>(
            make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = resources;
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, AgentFullConstructionDoesNotCreateSharedResourcesCycle) {
    std::weak_ptr<ben_gear::agent::SharedResources> weak;
    {
        ben_gear::agent::Agent agent(make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        weak = agent.resources();
        EXPECT_FALSE(weak.expired());
        EXPECT_TRUE(agent.resources()->workflow_engine() != nullptr);
        EXPECT_TRUE(agent.resources()->sub_agent_runtime() != nullptr);
    }
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, WorkflowResourcesDoNotStronglyOwnSharedResources) {
    std::weak_ptr<ben_gear::agent::SharedResources> weak;
    ben_gear::workflow::WorkflowResources workflow_resources;
    {
        ben_gear::agent::Agent agent(make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        auto resources = agent.resources();
        weak = resources;
        workflow_resources = resources->make_workflow_resources();
        EXPECT_TRUE(workflow_resources.is_bound());
        EXPECT_TRUE(workflow_resources.lifetime_context == nullptr);
    }
    // WorkflowResources intentionally keeps only non-owning pointers plus weak-bound callables.
    // Holding a copied WorkflowResources must not keep the root SharedResources alive.
    EXPECT_TRUE(weak.expired());
}

TEST_F(LifecycleTest, SubAgentRuntimeDoesNotStronglyOwnSharedResources) {
    std::weak_ptr<ben_gear::agent::SharedResources> weak;
    std::weak_ptr<ben_gear::agent::SubAgentRuntime> weak_runtime;
    {
        ben_gear::agent::Agent agent(make_lifecycle_settings(dir()), make_lifecycle_ws_ctx(dir()));
        auto resources = agent.resources();
        weak = resources;
        weak_runtime = resources->sub_agent_runtime();
        EXPECT_FALSE(weak.expired());
        EXPECT_FALSE(weak_runtime.expired());
    }
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(weak_runtime.expired());
}
