#pragma once

#include "ben_gear/agent/tool.hpp"

#include <string_view>

namespace ben_gear::agent {

class AgentCallbacks {
public:
    virtual ~AgentCallbacks() = default;
    virtual void on_thinking(std::string_view) const {}
    virtual void on_token(std::string_view) const {}
    virtual void on_tool_call(const ToolCall&) const {}
    virtual void on_tool_result(const ToolCall&, const ToolResult&) const {}
};

class NullAgentCallbacks final : public AgentCallbacks {};

}  // namespace ben_gear::agent

namespace ben_gear {
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
}  // namespace ben_gear
