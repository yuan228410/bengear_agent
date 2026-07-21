#pragma once

#include "config/settings.hpp"
#include <vector>

#include <map>
#include <string>

namespace ben_gear::mcp {


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
inline std::string transport_type(const config::MCPServerConfig& cfg) {
    if (!cfg.url.empty()) {
        return std::string("http");
    }
    return std::string("stdio");
}

}  // namespace ben_gear::mcp
