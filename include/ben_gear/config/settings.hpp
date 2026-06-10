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
 int max_tool_steps = 50;
 std::string system_prompt;
 int command_timeout = 30;
 int workflow_timeout = 300;
 int workflow_status_timeout = 60;
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

struct Settings {
 /// 将当前 LLM 相关字段应用到 target（用于 failover 切换模型）
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
 container::String anthropic_api_version;
 bool reasoning = false;
 container::String display_name;

 // 配置中的 provider 名（如 "oneapi_claw"），用于 fallback key 对齐
 container::String config_provider_name;

 container::String username;
 container::String workspace_name;
 container::String session_id;

 // 备用模型链（按优先级排序），如 {"gpt-4o", "claude-3-5-sonnet-20241022"}
 // 备用模型链（按优先级排序），格式为 "provider:model_name"
 std::vector<std::string> fallback_models;
 // 预解析的 fallback 模型配置：provider:model_name → 完整 Settings
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
} // namespace ben_gear
