#pragma once

#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <optional>
#include <vector>

namespace ben_gear::llm {

/// 工具调用管理器（处理工具调用的完整流程）
class ToolCallManager {
public:
    explicit ToolCallManager(const ToolRegistry& registry) : registry_(registry) {}
    
    /// 从 OpenAI 响应中提取工具调用
    std::vector<ToolCallRequest> extract_openai_tool_calls(const Json& response) const {
        std::vector<ToolCallRequest> calls;
        
        if (!response.contains("choices") || !response["choices"].is_array()) {
            return calls;
        }
        
        for (const auto& choice : response["choices"]) {
            if (!choice.contains("message")) continue;
            
            const auto& message = choice["message"];
            if (!message.contains("tool_calls")) continue;
            
            for (const auto& tool_call : message["tool_calls"]) {
                try {
                    calls.push_back(ToolCallRequest::from_openai(tool_call));
                } catch (const std::exception& e) {
                    log::error_fmt("failed to parse openai tool call: {}", e.what());
                }
            }
        }
        
        return calls;
    }
    
    /// 从 Anthropic 响应中提取工具调用
    std::vector<ToolCallRequest> extract_anthropic_tool_calls(const Json& response) const {
        std::vector<ToolCallRequest> calls;
        
        if (!response.contains("content") || !response["content"].is_array()) {
            return calls;
        }
        
        for (const auto& block : response["content"]) {
            if (block.value("type", "") != "tool_use") continue;
            
            try {
                calls.push_back(ToolCallRequest::from_anthropic(block));
            } catch (const std::exception& e) {
                log::error_fmt("failed to parse anthropic tool call: {}", e.what());
            }
        }
        
        return calls;
    }
    
    /// 执行工具调用
    ToolCallResult execute_tool(const ToolCallRequest& request) const {
        ToolCallResult result;
        result.tool_call_id = request.id;
        result.name = request.name;

        auto exec = registry_.execute(request.name, request.arguments);
        result.success = exec.success;
        result.output = exec.success
            ? exec.output
            : container::String((std::string("Error: ") + std::string(exec.error.c_str())).c_str());

        return result;
    }
    
    /// 批量执行工具调用
    std::vector<ToolCallResult> execute_tools(const std::vector<ToolCallRequest>& requests) const {
        std::vector<ToolCallResult> results;
        results.reserve(requests.size());
        
        for (const auto& request : requests) {
            results.push_back(execute_tool(request));
        }
        
        return results;
    }
    
    /// 构建 OpenAI 工具结果消息
    Json build_openai_tool_results(const std::vector<ToolCallResult>& results) const {
        Json messages = Json::array();
        for (const auto& result : results) {
            messages.push_back(Json{
                {"role", "tool"},
                {"tool_call_id", result.tool_call_id},
                {"content", result.output}
            });
        }
        return messages;
    }
    
    /// 构建 Anthropic 工具结果消息
    Json build_anthropic_tool_results(const std::vector<ToolCallResult>& results) const {
        Json content = Json::array();
        for (const auto& result : results) {
            content.push_back(Json{
                {"type", "tool_result"},
                {"tool_use_id", result.tool_call_id},
                {"content", result.output}
            });
        }
        
        return Json{
            {"role", "user"},
            {"content", content}
        };
    }
    
    /// 检查响应是否包含工具调用
    static bool has_tool_calls(const Json& response, Provider provider) {
        if (provider == Provider::openai) {
            if (!response.contains("choices") || !response["choices"].is_array()) {
                return false;
            }
            for (const auto& choice : response["choices"]) {
                if (choice.contains("message") && 
                    choice["message"].contains("tool_calls") &&
                    !choice["message"]["tool_calls"].empty()) {
                    return true;
                }
            }
            return false;
        } else {  // anthropic
            if (!response.contains("content") || !response["content"].is_array()) {
                return false;
            }
            for (const auto& block : response["content"]) {
                if (block.value("type", "") == "tool_use") {
                    return true;
                }
            }
            return false;
        }
    }
    
private:
    const ToolRegistry& registry_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolCallManager = llm::ToolCallManager;
}  // namespace ben_gear
