#pragma once

#include <string>

#include "llm_settings.hpp"
#include "sub_agent_config.hpp"

namespace ben_gear::config {

/// Agent 运行配置
struct AgentSettings {
  int max_tool_steps = 200;
  int max_tool_calls = 200;
  int max_tool_calls_per_step = 50;
  int max_parallel_tools = 0;
  std::string system_prompt;
  int command_timeout = 30;
  config::SubAgentConfig sub_agent;
  bool inject_project_doc = false;
};

} // namespace ben_gear::config
