#include "test_framework.hpp"
#include "config/sub_agent_config.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "base/utils/json.hpp"

using Json = ben_gear::Json;

// ==================== SubAgentConfig 默认值测试 ====================

TEST(SubAgentConfigTest, Defaults) {
    ben_gear::config::SubAgentConfig cfg;
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
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
    EXPECT_EQ(static_cast<int>(ben_gear::config::SessionType::main), 0);
    EXPECT_EQ(static_cast<int>(ben_gear::config::SessionType::sub_agent), 1);
    EXPECT_EQ(static_cast<int>(ben_gear::config::SessionType::workflow), 2);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
