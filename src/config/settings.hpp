#pragma once

// ═══════════════════════════════════════════════════════════════════
//  配置类型 — 按职责分散到独立头文件中
//  本文件为聚合入口，兼容旧代码单头文件引入方式
// ═══════════════════════════════════════════════════════════════════

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "log/level.hpp"
#include "base/utils/string_utils.hpp"
#include "config/sub_agent_config.hpp"
#include "config/llm_settings.hpp"
#include "config/agent_settings.hpp"

namespace ben_gear::config {

// ─── 子配置（短期仍留在主文件，后续可逐一拆分）──────────────────────

struct LogSettings {
    log::Level level = log::Level::debug;
    std::string output = std::string("file");
    std::string file;
    std::string network_host;
    std::string network_port;
    int max_file_size_mb = 10;
    int max_rotated_files = 5;
};

struct MCPServerConfig {
    std::string command;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> env;
    std::string url;
    bool disabled = false;
};

struct ConnectionPoolSettings {
    unsigned int max_connections_per_host = 10;
    unsigned int idle_timeout_seconds = 30;
    unsigned int connect_timeout_seconds = 10;
    unsigned int response_timeout_seconds = 60;
    bool enable_keep_alive = true;
    bool enable_object_pool = true;
};

struct ThreadPoolSettings {
    int min_threads = 2;
    int max_threads = 8;
    int max_queue_size = 1024;
    int idle_timeout_ms = 5000;
    int overflow_policy = 0;
};

struct WorkflowSettings {
    int task_timeout = 600;
    int max_retries = 3;
    unsigned int retry_delay_ms = 1000;
};

struct MCPSettings {
    int read_buffer_size = 4096;
};

struct ContextPruneSettings {
    bool enabled = true;
    int protect_recent = 3;
    int soft_prune_lines = 5;
    int hard_prune_after = 10;
    int max_tool_result_chars = 2000;
};

struct ServerSettings {
    std::string host = std::string("0.0.0.0");
    int port = 8080;
    int max_concurrent_requests = 100;
    int session_idle_timeout_seconds = 1800;
    int agent_pool_max_size = 50;
    std::vector<std::string> cors_origins;
    std::string api_key;
    bool openai_compatible = true;
    std::string static_dir = std::string("./web/dist");
    bool daemon = false;
};

// ─── Settings 聚合 ────────────────────────────────────────────────

struct Settings {
    void apply_llm_fields_to(Settings& target) const {
        target.llm = llm;
    }

    LlmSettings llm;
    LogSettings logging;
    LlmRequestRetrySettings llm_request_retry;
    std::filesystem::path workspace;
    std::filesystem::path plugins_dir;
    std::unordered_map<std::string, MCPServerConfig> mcp_servers;
    AgentSettings agent;
    ConnectionPoolSettings connection_pool;
    ThreadPoolSettings thread_pool;
    WorkflowSettings workflow;
    MCPSettings mcp;
    ContextPruneSettings context_prune;
    ServerSettings server;
    std::string username;
    std::string workspace_name;
    std::string session_id;
    std::map<std::string, Settings> resolved_fallbacks;
};

// ─── 工具函数 ──────────────────────────────────────────────────────

inline bool parse_bool(std::string_view value) {
    const auto normalized = base::utils::to_lower(base::utils::trim(value));
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

inline int parse_positive_int(std::string_view value, int fallback) {
    const auto trimmed = base::utils::trim(value);
    int result = 0;
    auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), result);
    if (ec == std::errc{} && result > 0) return result;
    return fallback;
}

} // namespace ben_gear::config

// ─── 类型别名（兼容旧代码） ─────────────────────────────────────────

namespace ben_gear {
    using Config = config::Settings;
    using LogSettings = config::LogSettings;
    using LlmRequestRetrySettings = config::LlmRequestRetrySettings;
    using LlmSettings = config::LlmSettings;
    using Provider = config::Provider;
    using config::parse_bool;
    using config::parse_positive_int;
    using config::parse_provider;
    using config::provider_name;
    using SubAgentConfig = config::SubAgentConfig;
    using SessionType = config::SessionType;
}
