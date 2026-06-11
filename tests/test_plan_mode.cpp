#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/tool/registry.hpp"

using namespace ben_gear::agent;

// ============================================================
// PlanManager 两态状态机测试
// ============================================================

TEST(PlanManagerTest, DefaultModeIsNormal) {
    PlanManager pm;
    EXPECT_EQ(pm.mode(), PlanManager::Mode::normal);
    EXPECT_FALSE(pm.in_plan_mode());
    EXPECT_FALSE(pm.is_active());
}

TEST(PlanManagerTest, EnterPlanMode) {
    PlanManager pm;
    pm.enter_plan_mode();
    EXPECT_EQ(pm.mode(), PlanManager::Mode::planning);
    EXPECT_TRUE(pm.in_plan_mode());
    EXPECT_TRUE(pm.is_active());
}

TEST(PlanManagerTest, ExitPlanMode) {
    PlanManager pm;
    pm.enter_plan_mode();
    pm.exit_plan_mode();
    EXPECT_EQ(pm.mode(), PlanManager::Mode::normal);
    EXPECT_FALSE(pm.in_plan_mode());
    EXPECT_FALSE(pm.is_active());
}

TEST(PlanManagerTest, ToggleMode) {
    PlanManager pm;
    // normal → planning → normal → planning
    pm.enter_plan_mode();
    EXPECT_TRUE(pm.in_plan_mode());
    pm.exit_plan_mode();
    EXPECT_FALSE(pm.in_plan_mode());
    pm.enter_plan_mode();
    EXPECT_TRUE(pm.in_plan_mode());
}

// ============================================================
// AgentCallbacks 结构化事件测试
// ============================================================

TEST(PlanModeCallbacksTest, OnModeChangedCalled) {
    PlanManager::Mode last_mode = PlanManager::Mode::normal;
    int call_count = 0;

    class TestCallbacks : public AgentCallbacks {
    public:
        PlanManager::Mode& last_mode;
        int& call_count;
        TestCallbacks(PlanManager::Mode& m, int& c) : last_mode(m), call_count(c) {}
        void on_mode_changed(PlanManager::Mode mode) const override {
            last_mode = mode;
            ++call_count;
        }
    };

    TestCallbacks cb(last_mode, call_count);
    cb.on_mode_changed(PlanManager::Mode::planning);
    EXPECT_EQ(last_mode, PlanManager::Mode::planning);
    EXPECT_EQ(call_count, 1);

    cb.on_mode_changed(PlanManager::Mode::normal);
    EXPECT_EQ(last_mode, PlanManager::Mode::normal);
    EXPECT_EQ(call_count, 2);
}

TEST(PlanModeCallbacksTest, OnToolBlockedCalled) {
    std::string last_tool;
    std::string last_reason;

    class TestCallbacks : public AgentCallbacks {
    public:
        std::string& last_tool;
        std::string& last_reason;
        TestCallbacks(std::string& t, std::string& r) : last_tool(t), last_reason(r) {}
        void on_tool_blocked(std::string_view tool_name, std::string_view reason) const override {
            last_tool = std::string(tool_name);
            last_reason = std::string(reason);
        }
    };

    TestCallbacks cb(last_tool, last_reason);
    cb.on_tool_blocked("write_file", "read-only");
    EXPECT_EQ(last_tool, "write_file");
    EXPECT_EQ(last_reason, "read-only");
}

// ============================================================
// read_only 工具约束测试
// ============================================================

TEST(ReadOnlyTest, IsReadOnlyWorks) {
    ben_gear::llm::ToolRegistry registry;

    // 注册一个 read_only 工具
    registry.register_tool("read_file", "Read file", {}, nullptr);
    registry.set_read_only("read_file", true);

    // 注册一个非 read_only 工具
    registry.register_tool("write_file", "Write file", {}, nullptr);
    // write_file 默认不是 read_only

    EXPECT_TRUE(registry.is_read_only("read_file"));
    EXPECT_FALSE(registry.is_read_only("write_file"));
}

TEST(ReadOnlyTest, UnknownToolIsNotReadOnly) {
    ben_gear::llm::ToolRegistry registry;
    EXPECT_FALSE(registry.is_read_only("nonexistent_tool"));
}
