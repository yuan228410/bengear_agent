#pragma once

#include <string>
#include <unordered_map>

#include "base/utils/json.hpp"  // 提供 Json 类型

namespace ben_gear::agent::core {


/// 核心数据结构 — 与 5 大服务接口（IFileService / IWebAccessService / ...）配套的
/// 纯数据 payload，不包含行为。服务接口和 Agent 类定义在 agent_core.hpp 中。

struct SkillDefinition {
    std::string name;
    std::string description;
    std::string category;
    std::string version;
    std::unordered_map<std::string, std::string> parameters;
    std::unordered_map<std::string, std::string> metadata;
};

struct HttpRequest {
    std::string url;
    std::string method = std::string("GET");
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct CommandResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
    double exec_time_ms = 0.0;

    bool success() const noexcept { return exit_code == 0; }
};

struct MCPServerInfo {
    std::string name;
    std::string description;
    std::string base_url;
    bool requires_auth = false;
};

struct MCPToolDef {
    std::string name;
    std::string description;
    std::string server_name;
};

struct MCPEvent {
    std::string server_name;
    std::string type;
    Json data;
};

/// 核心异常
class CoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace ben_gear::agent::core
