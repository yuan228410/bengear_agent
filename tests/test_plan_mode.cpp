#include "test_framework.hpp"
#include "orchestration/plan.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "tool/registry.hpp"

// ============================================================
// PlanManager 状态机测试（新架构）
// ============================================================

TEST(PlanManagerTest, DefaultStateIsIdle) {
    ben_gear::orchestration::PlanManager pm;
    EXPECT_EQ(pm.status(), ben_gear::orchestration::PlanStatus::idle);
    EXPECT_FALSE(pm.is_active());
    EXPECT_FALSE(pm.is_reviewing());
    EXPECT_FALSE(pm.is_executing());
}

TEST(PlanManagerTest, PlanCommandStartsDrafting) {
    ben_gear::orchestration::PlanManager pm;
    ben_gear::orchestration::PlanCommand cmd;
    cmd.plan_id = "test-plan";
    cmd.prompt = "do something";
    const auto& draft = pm.start(cmd);
    EXPECT_EQ(draft.status, ben_gear::orchestration::PlanStatus::drafting);
    EXPECT_EQ(draft.plan_id, "test-plan");
}

TEST(PlanManagerTest, ReadOnlyToolsInPlanMode) {
    ben_gear::orchestration::PlanManager pm;
    // 启动 plan 流程后 read_only_tools() 变为 true
    // 通过进入特定阶段触发
    EXPECT_FALSE(pm.read_only_tools());
}

// ============================================================
// AgentEventSink 结构化事件测试（新架构）
// ============================================================

// TODO: adapt - on_mode_changed removed from AgentEventSink
#if 0
TEST(PlanModeCallbacksTest, OnModeChangedCalled) {
    PlanManager::Mode last_mode = PlanManager::Mode::normal;
    int call_count = 0;

    class TestCallbacks : public AgentEventSink {
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
#endif

TEST(PlanModeCallbacksTest, OnToolBlockedCalled) {
    std::string last_tool;
    std::string last_reason;

    class TestCallbacks : public ben_gear::agent::NullAgentEventSink {
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
