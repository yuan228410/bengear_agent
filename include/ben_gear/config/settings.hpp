#pragma once

#include "ben_gear/base/log/level.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/agent/sub_agent_config.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

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
 int max_tool_steps = 200;
 int max_tool_calls = 200;
 int max_tool_calls_per_step = 50;
 std::string system_prompt;
 int command_timeout = 30;
 int workflow_timeout = 300;
 int workflow_status_timeout = 60;
 agent::SubAgentConfig sub_agent;
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
};

struct WorkflowSettings {
 int task_timeout = 600;
 int max_retries = 3;
 unsigned int retry_delay_ms = 1000;
};

struct MCPSettings {
 int read_buffer_size = 4096;
};

/// 上下文裁剪配置
struct ContextPruneSettings {
 bool enabled = true;
 int protect_recent = 3;
 int soft_prune_lines = 5;
 int hard_prune_after = 10;
 int max_tool_result_chars = 2000;
};


/// Server 服务配置
struct ServerSettings {
 container::String host = container::String("0.0.0.0");
 int port = 8080;
 int max_concurrent_requests = 100;
 int session_idle_timeout_seconds = 1800;
 int agent_pool_max_size = 50;
 container::Vector<container::String> cors_origins;
 container::String api_key;
 bool openai_compatible = true;
 container::String static_dir = container::String("./web/dist");
 bool daemon = false;
};
struct Settings {
 void apply_llm_fields_to(Settings& target) const {
  target.provider = provider;
  target.api_key = api_key;
  target.base_url = base_url;
  target.api_url = api_url;
  target.model = model;
  target.max_tokens = max_tokens;
  target.temperature = temperature;
  target.context_length = context_length;
  target.headers = headers;
  target.anthropic_api_version = anthropic_api_version;
  target.reasoning = reasoning;
  target.display_name = display_name;
  target.config_provider_name = config_provider_name;
}

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
 ContextPruneSettings context_prune;
 ServerSettings server;
 container::String anthropic_api_version;
 bool reasoning = false;
 container::String display_name;
 container::String config_provider_name;
 container::String username;
 container::String workspace_name;
 container::String session_id;
 std::vector<std::string> fallback_models;
 std::map<std::string, Settings> resolved_fallbacks;
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

} // namespace ben_gear::config

namespace ben_gear {
using Config = config::Settings;
using LogSettings = config::LogSettings;
using LlmRequestRetrySettings = config::LlmRequestRetrySettings;
using Provider = config::Provider;
using config::parse_bool;
using config::parse_positive_int;
using config::parse_provider;
using config::provider_name;
}
