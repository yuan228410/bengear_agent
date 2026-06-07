#pragma once

#include "ben_gear/base/log/level.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace ben_gear::config {

namespace container = base::container;

enum class Provider { openai, anthropic };

struct LogSettings {
    log::Level level = log::Level::debug;
    container::String output = container::String("file");
    container::String file;
    container::String network_host;
    container::String network_port;
    int max_file_size_mb = 10;
    int max_rotated_files = 5;
};

struct LlmRequestRetrySettings {
    int max_attempts = 5;
    unsigned int initial_delay_ms = 200;
    unsigned int max_delay_ms = 3000;
};

struct MCPServerConfig {
    container::String command;
    container::Vector<container::String> args;
    std::map<std::string, std::string> env;
    container::String url;
    bool disabled = false;
};

struct AgentSettings {
    int max_tool_steps = 50;
    std::string system_prompt;  // 空=使用默认
    int command_timeout = 30;              // 工具调用默认超时（秒）
    int workflow_timeout = 300;            // execute_workflow 超时（秒），默认 5 分钟
    int workflow_status_timeout = 60;      // get_workflow_status 超时（秒），默认 1 分钟
};

struct ConnectionPoolSettings {
    unsigned int max_connections_per_host = 10;
    unsigned int idle_timeout_seconds = 30;
    unsigned int connect_timeout_seconds = 10;
    unsigned int response_timeout_seconds = 60;  // 读空闲超时：两次数据到达之间的最大间隔，防止服务端无响应时永久挂起
    bool enable_keep_alive = true;
    bool enable_object_pool = true;
};

struct ThreadPoolSettings {
    int min_threads = 2;
    int max_threads = 8;
    int max_queue_size = 1024;
    int idle_timeout_ms = 5000;
};

struct WorkflowSettings {
    int task_timeout = 600;               // ToolTask 超时（秒），默认 10 分钟
    int max_retries = 3;                  // 任务重试次数
    unsigned int retry_delay_ms = 1000;   // 重试延迟（毫秒）
};

struct MCPSettings {
    int read_buffer_size = 4096;
};

struct Settings {
    Provider provider = Provider::openai;
    container::String api_key;
    container::String base_url = container::String("https://api.openai.com");
    container::String api_url;
    container::String model = container::String("gpt-4o-mini");
    int max_tokens = 1024;
    double temperature = 0.2;
    bool stream = true;
    LogSettings logging;
    LlmRequestRetrySettings llm_request_retry;
    std::int64_t context_length = 256000;
    std::map<std::string, std::string> headers;
    std::filesystem::path workspace;
    std::map<std::string, MCPServerConfig> mcp_servers;
    AgentSettings agent;
    ConnectionPoolSettings connection_pool;
    ThreadPoolSettings thread_pool;
    WorkflowSettings workflow;
    MCPSettings mcp;
    container::String anthropic_api_version;  // 空=使用默认 "2026-01-01"
    bool reasoning = false;                 // 是否启用推理/思考模式
    container::String display_name;         // 模型显示名称（model_config 中的 name）

    // 多级管理字段
    container::String username;             // 当前用户名，默认 "default"
    container::String workspace_name;       // 当前工作空间名，默认 "default"
    container::String session_id;           // 当前会话 ID，空=新建
};

inline container::String provider_name(Provider provider) {
    return provider == Provider::anthropic ? container::String("anthropic") : container::String("openai");
}

inline Provider parse_provider(std::string_view value) {
    const auto normalized = base::utils::to_lower(base::utils::trim(value));
    return normalized == "anthropic" || normalized == "claude" ? Provider::anthropic : Provider::openai;
}

inline bool parse_bool(std::string_view value) {
    const auto normalized = base::utils::to_lower(base::utils::trim(value));
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

inline int parse_positive_int(std::string_view value, int fallback) {
    try {
        return std::max(1, std::stoi(std::string(base::utils::trim(value))));
    } catch (...) {
        return fallback;
    }
}

}  // namespace ben_gear::config

namespace ben_gear {
using Config = config::Settings;
using LogSettings = config::LogSettings;
using LlmRequestRetrySettings = config::LlmRequestRetrySettings;
using Provider = config::Provider;
using config::parse_bool;
using config::parse_positive_int;
using config::parse_provider;
using config::provider_name;
}  // namespace ben_gear
