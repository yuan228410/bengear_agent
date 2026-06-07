#pragma once

#include <string_view>

#include "ben_gear/tool/types.hpp"

namespace ben_gear::agent {

/// Agent 回调接口
class AgentCallbacks {
public:
    virtual ~AgentCallbacks() = default;
    virtual void on_token(std::string_view /*token*/) const {}
    virtual void on_thinking(std::string_view /*token*/) const {}
    virtual void on_tool_call(const llm::ToolCallRequest& /*call*/) const {}
    virtual void on_tool_result(const llm::ToolCallResult& /*result*/) const {}
};

/// 空回调实现
class NullAgentCallbacks : public AgentCallbacks {};

}  // namespace ben_gear::agent
