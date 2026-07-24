#include "base/core/event_filter.hpp"
#include "agent/core/events.hpp"

namespace ben_gear::base {

bool EventFilter::should_forward(std::type_index event_type) const {
    if (event_type == std::type_index(typeid(agent::TokenEvent))) return config_.include_tokens;
    if (event_type == std::type_index(typeid(agent::ThinkingEvent))) return config_.include_thinking;
    if (event_type == std::type_index(typeid(agent::ToolCallEvent))) return config_.include_tool_calls;
    if (event_type == std::type_index(typeid(agent::ToolResultEvent))) return config_.include_tool_calls;
    if (event_type == std::type_index(typeid(agent::ToolBlockedEvent))) return config_.include_tool_calls;
    if (event_type == std::type_index(typeid(agent::ResponseStatsEvent))) return config_.include_stats;
    if (event_type == std::type_index(typeid(agent::ExecutionPlanEvent))) return config_.include_exec_events;
    if (event_type == std::type_index(typeid(agent::TodoUpdateEvent))) return config_.include_todo;
    if (event_type == std::type_index(typeid(agent::SubAgentStartEvent))) return config_.include_sub_agent;
    if (event_type == std::type_index(typeid(agent::SubAgentProgressEvent))) return config_.include_sub_agent;
    if (event_type == std::type_index(typeid(agent::SubAgentCompleteEvent))) return config_.include_sub_agent;
    if (event_type == std::type_index(typeid(agent::SubAgentErrorEvent))) return config_.include_sub_agent;
    return true;  // 未知事件类型默认放行
}

std::unique_ptr<EventFilter> EventFilter::from_config(Config config) {
    return std::make_unique<EventFilter>(std::move(config));
}

} // namespace ben_gear::base