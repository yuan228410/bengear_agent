#pragma once

#include "base/log/level.hpp"
#include "base/utils/string_utils.hpp"
#include <unordered_map>
#include <map>
#include <vector>
#include "base/config/sub_agent_config.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ben_gear::config {

enum class Provider { openai, anthropic };

struct LogSettings {
 log::Level level = log::Level::debug;
 std::string output = std::string("file");
 std::string file;
 std::string network_host;
 std::string network_port;
 int max_file_size_mb = 10;
 int max_rotated_files = 5;
};

struct LlmRequestRetrySettings {
 int max_attempts = 5;
 unsigned int initial_delay_ms = 200;
 unsigned int max_delay_ms = 3000;
};

struct MCPServerConfig {
  std::string command;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> env;
  std::string url;
  bool disabled = false;
};

struct AgentSettings {
  int max_tool_steps = 200;
  int max_tool_calls = 200;
  int max_tool_calls_per_step = 50;
  int max_parallel_tools = 0;  // 0 = unlimited (bounded by thread pool)
  std::string system_prompt;
  int command_timeout = 30;
 int workflow_timeout = 300;
 int workflow_status_timeout = 60;
 config::SubAgentConfig sub_agent;
 bool inject_project_doc = false;  // 注入 AGENTS.md/CLAUDE.md 到系统提示
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
 int overflow_policy = 0;  // 0=Abort, 1=CallerRuns, 2=DiscardOldest
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
 std::string api_key;
 std::string base_url = std::string("https://api.openai.com");
 std::string api_url;
 std::string model = std::string("gpt-4o-mini");
 int max_tokens = 1024;
 double temperature = 0.2;
 bool stream = true;
 LogSettings logging;
 LlmRequestRetrySettings llm_request_retry;
  std::int64_t context_length = 256000;
  std::unordered_map<std::string, std::string> headers;
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
 std::string anthropic_api_version;
 bool reasoning = false;
 std::string display_name;
 std::string config_provider_name;
 std::string username;
 std::string workspace_name;
 std::string session_id;
  std::vector<std::string> fallback_models;
  std::map<std::string, Settings> resolved_fallbacks;
};

inline std::string provider_name(Provider provider) {
 return provider == Provider::anthropic ? std::string("anthropic") : std::string("openai");
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
  const auto trimmed = base::utils::trim(value);
  int result = 0;
  auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), result);
  if (ec == std::errc{} && result > 0) return result;
  return fallback;
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
using SubAgentConfig = config::SubAgentConfig;
using SessionType = config::SessionType;
}
