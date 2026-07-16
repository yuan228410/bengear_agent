#include "test_framework.hpp"
#include "agent/runtime/tool_context.hpp"
#include "agent/runtime/memory_context.hpp"
#include "agent/runtime/orchestration_context.hpp"
#include "test_util.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

// ==================== Mock Implementations ====================

/// Mock for IToolContext — owns real lightweight instances
struct MockToolContext : IToolContext {
    capabilities::tool::ToolRegistry registry_;
    std::shared_ptr<::ben_gear::mcp::MCPManager> mcp_{
        std::make_shared<::ben_gear::mcp::MCPManager>()};

    const capabilities::tool::ToolRegistry& registry() const override { return registry_; }
    capabilities::tool::ToolRegistry& registry_mut() override { return registry_; }
    const std::shared_ptr<::ben_gear::mcp::MCPManager>& mcp() const override { return mcp_; }
};

/// Mock for IMemoryContext — owns instances that need filesystem resources
struct MockMemoryContext : IMemoryContext {
    std::shared_ptr<memory::MemoryStore> store_;
    std::unique_ptr<memory::ContextBuilder> builder_;
    std::unique_ptr<workspace::HistoryDB> history_db_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;

    explicit MockMemoryContext(const std::filesystem::path& tmp_dir) {
        std::filesystem::create_directories(tmp_dir / "global");
        std::filesystem::create_directories(tmp_dir / "user");
        std::filesystem::create_directories(tmp_dir / "ws");
        ben_gear::base::TierPaths tp{tmp_dir / "global", tmp_dir / "user", tmp_dir / "ws"};
        store_ = std::make_shared<memory::MemoryStore>(tp);
        builder_ = std::make_unique<memory::ContextBuilder>(*store_);
        history_db_ = std::make_unique<workspace::HistoryDB>(tmp_dir / "history.db");
        ws_manager_ = std::make_shared<workspace::WorkspaceManager>(tmp_dir / "user");
    }

    const std::shared_ptr<memory::MemoryStore>& store() const override { return store_; }
    const std::unique_ptr<memory::ContextBuilder>& builder() const override { return builder_; }
    workspace::HistoryDB& history_db() override { return *history_db_; }
    const std::shared_ptr<workspace::WorkspaceManager>& ws_manager() const override { return ws_manager_; }
};

/// Mock for IOrchestrationContext — uses default-constructed instances
struct MockOrchestrationContext : IOrchestrationContext {
    std::shared_ptr<workflow::WorkflowEngine> workflow_{
        std::make_shared<workflow::WorkflowEngine>()};
    std::shared_ptr<workflow::WorkflowTemplateLibrary> templates_{
        std::make_shared<workflow::WorkflowTemplateLibrary>()};
    orchestration::PlanManager plans_;
    std::unique_ptr<plugins::PluginLoader> plugin_loader_{
        std::make_unique<plugins::PluginLoader>()};

    const std::shared_ptr<workflow::WorkflowEngine>& workflow() const override { return workflow_; }
    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& templates() const override { return templates_; }
    orchestration::PlanManager& plans() override { return plans_; }
    const orchestration::PlanManager& plans() const override { return plans_; }
    const std::unique_ptr<plugins::PluginLoader>& plugin_loader() const override { return plugin_loader_; }
};

} // namespace ben_gear::agent::runtime

using namespace ben_gear::agent::runtime;

// ==================== IToolContext Tests ====================

TEST(ContextFacade, ToolContext_DispatchThroughInterface) {
    MockToolContext mock;
    IToolContext* iface = &mock;

    // registry() returns reference
    const auto& reg = iface->registry();
    EXPECT_EQ(reg.size(), 0u);   // fresh registry is empty
    EXPECT_FALSE(reg.has_tool("nonexistent"));

    // registry_mut() returns mutable reference — can register a tool
    auto& mut_reg = iface->registry_mut();
    mut_reg.register_tool("test_tool", "a test tool", {}, [](const auto&) { return "ok"; }, true);
    EXPECT_TRUE(iface->registry().has_tool("test_tool"));
    EXPECT_EQ(iface->registry().size(), 1u);

    // mcp() returns shared_ptr to MCPManager
    const auto& mcp_ptr = iface->mcp();
    EXPECT_NE(mcp_ptr, nullptr);
}

TEST(ContextFacade, ToolContext_ConstOverloads) {
    const MockToolContext mock;
    const IToolContext* iface = &mock;

    // All const methods dispatch correctly
    EXPECT_NO_THROW(iface->registry());
    EXPECT_NO_THROW(iface->mcp());

    const auto& reg = iface->registry();
    EXPECT_TRUE(reg.empty());
}

// ==================== IMemoryContext Tests ====================

class MemoryContextTest : public bengear::test::TmpDirTest {};

TEST_F(MemoryContextTest, DispatchThroughInterface) {
    MockMemoryContext mock(dir());
    IMemoryContext* iface = &mock;

    // store() returns shared_ptr to MemoryStore
    const auto& store = iface->store();
    EXPECT_NE(store, nullptr);

    // builder() returns unique_ptr to ContextBuilder
    const auto& builder = iface->builder();
    EXPECT_NE(builder, nullptr);

    // history_db() returns non-null reference
    auto& hist = iface->history_db();
    (void)hist;

    // ws_manager() returns shared_ptr
    const auto& ws = iface->ws_manager();
    EXPECT_NE(ws, nullptr);
}

TEST_F(MemoryContextTest, ConstOverloads) {
    const MockMemoryContext mock(dir());
    const IMemoryContext* iface = &mock;

    EXPECT_NO_THROW(iface->store());
    EXPECT_NO_THROW(iface->builder());
    EXPECT_NO_THROW(iface->ws_manager());
    // history_db() is non-const — not callable on const iface
}

TEST_F(MemoryContextTest, SameInstanceAcrossCalls) {
    MockMemoryContext mock(dir());
    IMemoryContext* iface = &mock;

    const auto& s1 = iface->store();
    const auto& s2 = iface->store();
    EXPECT_EQ(s1.get(), s2.get());  // same pointer each call

    const auto& b1 = iface->builder();
    const auto& b2 = iface->builder();
    EXPECT_EQ(b1.get(), b2.get());
}

// ==================== IOrchestrationContext Tests ====================

TEST(ContextFacade, OrchestrationContext_DispatchThroughInterface) {
    MockOrchestrationContext mock;
    IOrchestrationContext* iface = &mock;

    // workflow() returns shared_ptr to WorkflowEngine
    const auto& wf = iface->workflow();
    EXPECT_NE(wf, nullptr);

    // templates() returns shared_ptr to WorkflowTemplateLibrary
    const auto& tmpl = iface->templates();
    EXPECT_NE(tmpl, nullptr);

    // plans() returns reference to PlanManager — mutable call
    auto& pl = iface->plans();
    EXPECT_FALSE(pl.is_active());  // fresh plan manager has no active plan

    // plugin_loader() returns unique_ptr
    const auto& pl_loader = iface->plugin_loader();
    EXPECT_NE(pl_loader, nullptr);
}

TEST(ContextFacade, OrchestrationContext_ConstOverloads) {
    const MockOrchestrationContext mock;
    const IOrchestrationContext* iface = &mock;

    EXPECT_NO_THROW(iface->workflow());
    EXPECT_NO_THROW(iface->templates());
    EXPECT_NO_THROW(iface->plans());          // const plans() overload
    EXPECT_NO_THROW(iface->plugin_loader());
}

TEST(ContextFacade, OrchestrationContext_ConstVsMutablePlans) {
    MockOrchestrationContext mock;
    IOrchestrationContext* iface = &mock;

    // Mutable plans() — can modify
    auto& mut_plans = iface->plans();
    EXPECT_FALSE(mut_plans.is_active());

    // Const plans() via const pointer — same underlying PlanManager
    const IOrchestrationContext* const_iface = &mock;
    const auto& const_plans = const_iface->plans();
    EXPECT_FALSE(const_plans.is_active());
    // Both point to the same PlanManager instance
    EXPECT_EQ(&mut_plans, &const_plans);
}

// ==================== Interface Polymorphism ====================

TEST(ContextFacade, PolymorphicDelete) {
    // Verify virtual destructor works — delete through base pointer
    {
        auto* tc = static_cast<IToolContext*>(new MockToolContext());
        delete tc;  // no leak, no UB
    }
    {
        auto* oc = static_cast<IOrchestrationContext*>(new MockOrchestrationContext());
        delete oc;
    }
    EXPECT_TRUE(true);  // reached without crashing
}

TEST(ContextFacade, CrossInterfaceIndependence) {
    MockToolContext tc;
    MockOrchestrationContext oc;

    // Modifying one interface doesn't affect the other
    tc.registry_mut().register_tool("x", "desc", {}, [](const auto&) { return "ok"; });
    EXPECT_TRUE(tc.registry().has_tool("x"));

    // Orchestration context is unaffected
    EXPECT_FALSE(oc.plans().is_active());
}
