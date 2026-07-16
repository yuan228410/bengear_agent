#pragma once

#include "capabilities/tool/registry.hpp"
#include "capabilities/mcp/mcp_client.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

/// 工具子系统：工具注册表 + MCP 管理器
struct ToolContext {
    capabilities::tool::ToolRegistry registry;
    std::shared_ptr<::ben_gear::mcp::MCPManager> mcp;

    ToolContext(size_t mcp_read_buffer_size = 65536)
        : mcp(std::make_shared<::ben_gear::mcp::MCPManager>(mcp_read_buffer_size)) {}
};

} // namespace ben_gear::agent::runtime
