#include "test_framework.hpp"
#include "agent/runtime/runtime.hpp"
#include "agent/runtime/runtime_factory.hpp"
#include "base/utils/json.hpp"
#include "capabilities/tool/registry.hpp"
#include "config/settings.hpp"
#include "workspace/manager.hpp"
#include "test_util.hpp"

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

class AgentResourceTest : public TmpDirTest {};

TEST_F(AgentResourceTest, RegisterCustomTool) {
    ben_gear::config::Settings settings;
    auto ws_ctx = make_test_ws_ctx(dir());
    
    auto agent = ben_gear::agent::runtime::RuntimeFactory::create_uninitialized(
        std::move(settings), std::move(ws_ctx));
    
    // 注册自定义工具
    std::vector<std::pair<std::string, ben_gear::capabilities::tool::ToolParameterSchema>> params;
    params.push_back({
        std::string("input"),
        ben_gear::capabilities::tool::ToolParameterSchema{
            std::string("string"),
            std::string("Input text")
        }
    });
    
    auto* tool_reg = agent->services().resolve<ben_gear::capabilities::tool::ToolRegistry>();
    tool_reg->register_tool(
        std::string("custom_tool"),
        std::string("A custom tool for testing"),
        params,
        [](const ben_gear::Json& args) -> std::string {
            (void)args;
            return std::string("custom_result");
        }
    );
    
    // 验证工具已注册
    EXPECT_TRUE(tool_reg->find("custom_tool").has_value());
}
