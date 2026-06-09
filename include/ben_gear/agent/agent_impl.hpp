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
    // ==================== 核心系统提示词（写死，不允许修改） ====================

    /// 普通模式：引导 LLM 复杂任务自动规划步骤
    static constexpr std::string_view kPlanGuidancePrompt =
        "When a task is complex (3+ steps), break it down into a plan BEFORE executing.\n"
        "Output your plan in this exact format:\n"
        "## Plan\n"
        "1. Step description\n"
        "2. Step description\n"
        "...\n"
        "Do NOT execute any steps until the user confirms the plan.\n"
        "For simple tasks (1-2 steps), just execute directly without a plan.\n";

    /// 计划模式：禁止工具，只讨论方案
    static constexpr std::string_view kPlanModePrompt =
        "You are now in PLAN MODE.\n"
        "1. Discuss the solution with the user, answer questions, refine the plan\n"
        "2. Output a numbered list of steps when the plan is finalized\n"
        "3. Do NOT use any tools — only discuss and design\n"
        "4. Format your plan as:\n"
        "   ## Plan\n"
        "   1. Step description\n"
        "   2. Step description\n"
        "   ...\n"
        "Wait for the user to approve the plan before any execution.\n";

    // =============================================================================
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

        // 核心规划引导（写死，不可配置）
        prompt.append(kPlanGuidancePrompt.data(), kPlanGuidancePrompt.size());
        prompt += "\n";

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
        
        // 创建 assistant 消息（使用 data() 避免额外的 strlen 调用）
        auto acp_msg = acp::ACPMessage::assistant_message(
            base::container::String(content.data(), content.size()));
        
        // 添加工具调用
        for (const auto& call : calls) {
            acp_msg.add_tool_use(call);
        }
        
        history.add_message(acp_msg);
    }
};

}  // namespace ben_gear::agent
