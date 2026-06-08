#pragma once

#include "ben_gear/agent/agent.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::agent {

/// Agent 私有实现细节
/// 这些方法仅供 Agent 内部使用，不对外暴露
class AgentImpl {
public:
    /// 构建系统提示（分层组装）
    static std::string build_system_prompt(const SharedResources& resources) {
        if (resources.context_builder()) {
            return resources.context_builder()->build();
        }
        
        std::string prompt;
        auto sp = resources.settings().agent.system_prompt;
        
        // 预估大小，避免多次 realloc
        size_t estimated_size = 256;  // 基础提示词大小
        if (!sp.empty()) {
            estimated_size += sp.size() + 2;  // +2 for "\n\n"
        }
        auto skills_meta = resources.skill_loader().get_skills_metadata();
        if (!skills_meta.empty()) {
            estimated_size += skills_meta.size() + 100;  // +100 for 额外说明
        }
        prompt.reserve(estimated_size);
        
        // 使用 += 而不是 +，避免临时对象
        if (!sp.empty()) {
            prompt.append(sp.data(), sp.size());
            prompt += "\n\n";
        } else {
            prompt = "You are BenGear, a concise cross-platform coding agent. "
                     "Prefer direct, actionable answers and avoid unnecessary dependencies.\n\n";
        }
        
        if (!skills_meta.empty()) {
            prompt.append(skills_meta.data(), skills_meta.size());
            prompt += "\nTo use a skill, call the get_skill tool with the skill name. "
                      "This loads detailed instructions into the conversation.\n";
        }
        
        return prompt;
    }

    /// 发送思考内容到回调
    static void emit_thinking(const Json& response, 
                              const AgentCallbacks& callbacks,
                              config::Provider provider) {
        if (provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                Json choices = response["choices"]; Json message = choices[0]["message"];
                if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
                    callbacks.on_thinking(message["reasoning_content"].get<std::string>());
                }
            }
        } else {
            if (response.contains("content") && response["content"].is_array()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "thinking") {
                        if (block.contains("thinking") && !block["thinking"].is_null()) {
                            callbacks.on_thinking(block["thinking"].get<std::string>());
                        }
                    }
                }
            }
        }
    }

    /// 提取响应文本
    static std::string extract_response_text(const Json& response, config::Provider provider) {
        if (provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                // 分步访问避免 const Json 链式 operator[] 产生临时对象
                Json choices = response["choices"];
                Json message = choices[0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
        } else {
            if (response.contains("content") && response["content"].is_array() && !response["content"].empty()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        return block.value("text", "");
                    }
                }
            }
        }
        return "";
    }

    /// 提取工具调用
    static std::vector<llm::ToolCallRequest> extract_tool_calls(
            const Json& response,
            const llm::ToolCallManager& tool_manager,
            config::Provider provider) {
        if (provider == config::Provider::openai) {
            return tool_manager.extract_openai_tool_calls(response);
        } else {
            return tool_manager.extract_anthropic_tool_calls(response);
        }
    }

    /// 添加带工具调用的助手消息到历史记录
    static void add_assistant_message_with_tools_to(
            const Json& response,
            const std::vector<llm::ToolCallRequest>& calls,
            llm::ConversationHistory& history,
            config::Provider provider) {
        std::string content;
        
        if (provider == config::Provider::openai) {
            Json msg = response["choices"][0]["message"];
            content = msg.value("content", "");
        } else {
            if (response.contains("content") && response["content"].is_array()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        content = block.value("text", "");
                        break;
                    }
                }
            }
        }
        
        // 使用统一的构建函数
        history.add_message(build_assistant_message_with_tools(std::string_view(content), calls));
    }

    /// 构建带工具调用的助手消息（统一处理，避免重复）
    static llm::Message build_assistant_message_with_tools(
            std::string_view content,
            const std::vector<llm::ToolCallRequest>& calls) {
        llm::Message msg;
        msg.role = llm::MessageRole::assistant;
        msg.content = base::container::String(content);
        msg.blocks = convert_tool_calls_to_blocks(calls);
        return msg;
    }

    /// 转换工具调用为内容块
    static base::container::Vector<llm::ContentBlock> convert_tool_calls_to_blocks(
            const std::vector<llm::ToolCallRequest>& calls) {
        base::container::Vector<llm::ContentBlock> blocks;
        for (auto call : calls) {
            blocks.push_back(llm::ContentBlock::tool_use_block(call));
        }
        return blocks;
    }
};

}  // namespace ben_gear::agent
