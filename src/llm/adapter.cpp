#include "ben_gear/llm/adapter.hpp"

namespace ben_gear::llm {

// ==================== OpenAI 适配器实现 ====================

Json OpenAIAdapter::to_openai_format(const acp::ACPMessage& msg) {
    Json j;
    j["role"] = role_to_openai(msg.role());
    
    // 处理 Tool 角色的消息（工具返回结果）
    if (msg.role() == acp::Role::Tool) {
        // Tool 消息必须有 tool_call_id
        auto tool_results = msg.get_tool_results();
        if (!tool_results.empty()) {
            // OpenAI 要求每个 tool result 单独一条消息
            // 这里取第一个（如果有多个，需要调用方拆分）
            j["tool_call_id"] = tool_results[0].tool_call_id;
            j["content"] = tool_results[0].output;
        } else {
            // 没有 tool_result，设置空内容
            j["content"] = "";
        }
        return j;
    }
    
    // 处理内容块
    const auto& content = msg.content();
    
    if (content.empty()) {
        j["content"] = "";
        return j;
    }
    
    // 如果只有一个文本块，直接设置 content
    if (content.size() == 1 && content[0].is_text()) {
        j["content"] = content[0].text();
        return j;
    }
    
    // 多个内容块或包含工具调用
    j["content"] = msg.get_all_text();
    
    // 处理工具调用
    if (msg.has_tool_calls()) {
        Json tool_calls = Json::array();
        auto calls = msg.get_tool_calls();
        
        for (const auto& call : calls) {
            tool_calls.push_back({
                {"id", call.id},
                {"type", "function"},
                {"function", {
                    {"name", call.name},
                    {"arguments", call.arguments.dump()}
                }}
            });
        }
        
        j["tool_calls"] = tool_calls;
    }
    
    return j;
}

acp::ACPMessage OpenAIAdapter::from_openai_format(const Json& j) {
    acp::ACPMessage msg;
    
    // 角色
    auto role_str = j.value("role", "user");
    msg.set_role(role_from_openai(role_str));
    
    // 处理 Tool 角色的消息
    if (msg.role() == acp::Role::Tool) {
        if (j.contains("tool_call_id") && j.contains("content")) {
            llm::ToolCallResult result;
            result.tool_call_id = j.value("tool_call_id", "");
            result.output = j.value("content", "");
            msg.add_tool_result(std::move(result));
        }
        return msg;
    }
    
    // 内容
    if (j.contains("content") && !j["content"].is_null()) {
        msg.add_text(j["content"].get<container::String>());
    }
    
    // 工具调用
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
        for (const auto& tool_call : j["tool_calls"]) {
            llm::ToolCallRequest call;
            call.id = tool_call.value("id", "");
            
            if (tool_call.contains("function")) {
                auto func = tool_call["function"];
                call.name = func.value("name", "");
                
                // 解析 arguments
                if (func.contains("arguments")) {
                    auto args_str = func["arguments"].get<std::string>();
                    call.arguments = Json::parse(args_str);
                }
            }
            
            msg.add_tool_use(std::move(call));
        }
    }
    
    return msg;
}

Json OpenAIAdapter::to_openai_messages(const container::Vector<acp::ACPMessage>& messages) {
    Json result = Json::array();
    for (const auto& msg : messages) {
        // 特殊处理 Tool 角色：如果有多个 tool_result，需要拆分为多条消息
        if (msg.role() == acp::Role::Tool) {
            auto tool_results = msg.get_tool_results();
            if (tool_results.empty()) {
                // 没有 tool_result，添加空消息
                Json j;
                j["role"] = "tool";
                j["content"] = "";
                result.push_back(j);
            } else {
                // 每个 tool_result 单独一条消息（OpenAI 要求）
                for (const auto& tr : tool_results) {
                    Json j;
                    j["role"] = "tool";
                    j["tool_call_id"] = tr.tool_call_id;
                    j["content"] = tr.output;
                    result.push_back(j);
                }
            }
        } else {
            // 其他角色正常处理
            result.push_back(to_openai_format(msg));
        }
    }
    return result;
}

const char* OpenAIAdapter::role_to_openai(acp::Role role) {
    switch (role) {
        case acp::Role::System: return "system";
        case acp::Role::User: return "user";
        case acp::Role::Assistant: return "assistant";
        case acp::Role::Tool: return "tool";
        default: return "user";
    }
}

acp::Role OpenAIAdapter::role_from_openai(const container::String& role) {
    if (role == "system") return acp::Role::System;
    if (role == "user") return acp::Role::User;
    if (role == "assistant") return acp::Role::Assistant;
    if (role == "tool") return acp::Role::Tool;
    return acp::Role::User;
}

// ==================== Anthropic 适配器实现 ====================

Json AnthropicAdapter::to_anthropic_format(const acp::ACPMessage& msg) {
    Json j;
    j["role"] = role_to_anthropic(msg.role());
    
    // Anthropic 使用 content 数组
    j["content"] = Json::array();
    
    for (const auto& block : msg.content()) {
        if (block.is_text()) {
            j["content"].push_back({
                {"type", "text"},
                {"text", block.text()}
            });
        } else if (block.is_tool_use()) {
            auto& call = block.tool_use();
            j["content"].push_back({
                {"type", "tool_use"},
                {"id", call.id},
                {"name", call.name},
                {"input", call.arguments}
            });
        } else if (block.is_tool_result()) {
            auto& result = block.tool_result();
            j["content"].push_back({
                {"type", "tool_result"},
                {"tool_use_id", result.tool_call_id},
                {"content", result.output}
            });
        }
    }
    
    return j;
}

acp::ACPMessage AnthropicAdapter::from_anthropic_format(const Json& j) {
    acp::ACPMessage msg;
    
    // 角色
    auto role_str = j.value("role", "user");
    msg.set_role(role_from_anthropic(role_str));
    
    // 内容
    if (j.contains("content")) {
        if (j["content"].is_string()) {
            msg.add_text(j["content"].get<container::String>());
        } else if (j["content"].is_array()) {
            for (const auto& block : j["content"]) {
                auto type = block.value("type", "text");
                
                if (type == "text") {
                    msg.add_text(block.value("text", ""));
                } else if (type == "tool_use") {
                    llm::ToolCallRequest call;
                    call.id = block.value("id", "");
                    call.name = block.value("name", "");
                    call.arguments = block.value("input", Json::object());
                    msg.add_tool_use(std::move(call));
                } else if (type == "tool_result") {
                    llm::ToolCallResult result;
                    result.tool_call_id = block.value("tool_use_id", "");
                    result.output = block.value("content", "");
                    msg.add_tool_result(std::move(result));
                }
            }
        }
    }
    
    return msg;
}

Json AnthropicAdapter::to_anthropic_messages(const container::Vector<acp::ACPMessage>& messages) {
    Json result = Json::array();
    for (const auto& msg : messages) {
        // Anthropic 不在 messages 中包含 system
        if (msg.role() != acp::Role::System) {
            result.push_back(to_anthropic_format(msg));
        }
    }
    return result;
}

container::String AnthropicAdapter::extract_system_prompt(const container::Vector<acp::ACPMessage>& messages) {
    for (const auto& msg : messages) {
        if (msg.role() == acp::Role::System) {
            return msg.get_all_text();
        }
    }
    return container::String();
}

const char* AnthropicAdapter::role_to_anthropic(acp::Role role) {
    switch (role) {
        case acp::Role::System: return "system";
        case acp::Role::User: return "user";
        case acp::Role::Assistant: return "assistant";
        case acp::Role::Tool: return "user";  // Anthropic 用 user 表示 tool result
        default: return "user";
    }
}

acp::Role AnthropicAdapter::role_from_anthropic(const container::String& role) {
    if (role == "system") return acp::Role::System;
    if (role == "user") return acp::Role::User;
    if (role == "assistant") return acp::Role::Assistant;
    return acp::Role::User;
}

} // namespace ben_gear::llm
