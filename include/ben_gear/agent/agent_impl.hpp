#pragma once

#include "ben_gear/agent/agent.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::agent {

/// Agent 私有实现细节
class AgentImpl {
public:
    // ==================== 核心系统提示词 ====================

    /// 计划模式：read-only 探索约束
    static constexpr std::string_view kPlanModePrompt =
        "PLAN MODE \xe2\x80\x94 explore only, no modifications.\n" // —
        "You can read, search, and inspect freely.\n"
        "Write operations are disabled.\n"
        "Discuss your approach with the user. When ready to execute, ask the user to exit plan mode.\n";

    // ============================================================
    /// 构建系统提示（分层组装）
    static std::string build_system_prompt(const SharedResources& resources) {
        if (resources.context_builder()) {
            return resources.context_builder()->build();
        }

        std::string prompt;
        auto sp = resources.settings().agent.system_prompt;

        size_t estimated_size = 256;
        if (!sp.empty()) {
            estimated_size += sp.size() + 2;
        }
        auto skills_meta = resources.skill_loader().get_skills_metadata();
        if (!skills_meta.empty()) {
            estimated_size += skills_meta.size() + 100;
        }
        prompt.reserve(estimated_size);

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
        auto thinking = extract_thinking(response, provider);
        if (!thinking.empty()) callbacks.on_thinking(thinking);
    }

    /// 提取思考内容文本
    static std::string extract_thinking(const Json& response, config::Provider provider) {
        if (provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                Json choices = response["choices"]; Json message = choices[0]["message"];
                if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
                    return message["reasoning_content"].get<std::string>();
                }
            }
        } else {
            if (response.contains("content") && response["content"].is_array()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "thinking") {
                        if (block.contains("thinking") && !block["thinking"].is_null()) {
                            return block["thinking"].get<std::string>();
                        }
                    }
                }
            }
        }
        return "";
    }

    /// 提取响应文本
    static std::string extract_response_text(const Json& response, config::Provider provider) {
        if (provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
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
        workspace::ConversationHistory& history,
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

        auto acp_msg = acp::ACPMessage::assistant_message(
            base::container::String(content.data(), content.size()));

        for (const auto& call : calls) {
            acp_msg.add_tool_use(call);
        }

        history.add_message(acp_msg);
    }
};

} // namespace ben_gear::agent
