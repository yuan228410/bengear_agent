#pragma once

#include "capabilities/tool/registry.hpp"
#include "capabilities/mcp/mcp_client.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

/// Abstract interface for tool subsystem — enables mock injection for testing
struct IToolContext {
    virtual ~IToolContext() = default;
    virtual const capabilities::tool::ToolRegistry& registry() const = 0;
    virtual capabilities::tool::ToolRegistry& registry_mut() = 0;
    virtual const std::shared_ptr<::ben_gear::mcp::MCPManager>& mcp() const = 0;
};

/// Concrete tool subsystem: tool registry + MCP manager
struct ToolContext : IToolContext {
    capabilities::tool::ToolRegistry registry_;
    std::shared_ptr<::ben_gear::mcp::MCPManager> mcp_;

    ToolContext(int mcp_read_buffer_size = 65536)
        : mcp_(std::make_shared<::ben_gear::mcp::MCPManager>(mcp_read_buffer_size)) {}

    const capabilities::tool::ToolRegistry& registry() const override { return registry_; }
    capabilities::tool::ToolRegistry& registry_mut() override { return registry_; }
    const std::shared_ptr<::ben_gear::mcp::MCPManager>& mcp() const override { return mcp_; }
};

} // namespace ben_gear::agent::runtime
