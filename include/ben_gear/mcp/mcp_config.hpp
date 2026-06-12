#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <map>
#include <string>

namespace ben_gear::mcp {

namespace container = base::container;

/// 从 Settings 获取已启用的 MCP 服务器配置
inline std::map<std::string, config::MCPServerConfig> get_enabled_servers(
    const config::Settings& settings) {
    std::map<std::string, config::MCPServerConfig> enabled;
    for (const auto& [name, cfg] : settings.mcp_servers) {
        if (!cfg.disabled) {
            enabled[name] = cfg;
        }
    }
    return enabled;
}

/// 获取 MCP 服务器传输类型
inline container::String transport_type(const config::MCPServerConfig& cfg) {
    if (!cfg.url.empty()) {
        return container::String("http");
    }
    return container::String("stdio");
}

}  // namespace ben_gear::mcp
