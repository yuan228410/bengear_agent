#include "test_framework.hpp"
#include "orchestration/plan.hpp"
#include "capabilities/tool/registry.hpp"

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
// read_only 工具约束测试
// ============================================================

TEST(ReadOnlyTest, IsReadOnlyWorks) {
    ben_gear::capabilities::tool::ToolRegistry registry;

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
    ben_gear::capabilities::tool::ToolRegistry registry;
    EXPECT_FALSE(registry.is_read_only("nonexistent_tool"));
}
