#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/tool/registry.hpp"

namespace ben_gear::acp {

// 使用命名空间别名简化代码
namespace container = base::container;
namespace llm_tool = ben_gear::llm;  // 使用 llm 命名空间中的工具相关类型

// ==================== 工具适配器 ====================

/// 将 ACP 工具调用适配到工具注册表
class ToolAdapter {
public:
    // ==================== 构造函数 ====================
    
    explicit ToolAdapter(llm_tool::ToolRegistry& registry)
        : registry_(registry) {}
    
    // ==================== 工具调用 ====================
    
    /// 执行工具调用
    llm_tool::ToolCallResult execute_tool(const llm_tool::ToolCallRequest& call) {
        llm_tool::ToolCallResult result;
        result.tool_call_id = call.id;
        result.name = call.name;
        
        try {
            // 从注册表获取工具
            auto tool_entry = registry_.find(std::string(call.name));
            if (!tool_entry) {
                result.success = false;
                result.output = container::String("Tool not found: ") + call.name;
                return result;
            }
            
            // 执行工具
            auto tool_result = registry_.execute(std::string(call.name), call.arguments);
            result.success = tool_result.success;
            result.output = tool_result.output;
            
        } catch (const std::exception& e) {
            result.success = false;
            result.output = container::String(e.what());
        }
        
        return result;
    }
    
    /// 批量执行工具调用
    container::Vector<llm_tool::ToolCallResult> execute_tools(
        const container::Vector<llm_tool::ToolCallRequest>& calls) {
        
        container::Vector<llm_tool::ToolCallResult> results;
        results.reserve(calls.size());
        
        for (const auto& call : calls) {
            results.push_back(execute_tool(call));
        }
        
        return results;
    }
    
    // ==================== 工具定义转换 ====================
    
    /// 工具定义 → ACP 格式
    Json tool_definition_to_acp(const llm_tool::ToolDefinition& def) {
        Json schema;
        schema["name"] = def.name;
        schema["description"] = def.description;
        schema["input_schema"] = def.to_openai_format();
        return schema;
    }
    
    /// 获取所有工具定义（ACP 格式）
    Json get_all_tools_acp() {
        Json tools = Json::array();
        
        auto openai_tools = registry_.to_openai_tools();
        for (const auto& tool : openai_tools) {
            tools.push_back(tool);
        }
        
        return tools;
    }
    
private:
    llm_tool::ToolRegistry& registry_;
};

} // namespace ben_gear::acp
