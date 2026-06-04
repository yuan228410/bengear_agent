#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/manager.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/tools/skill_tools.hpp"
#include "ben_gear/mcp/mcp_client.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <chrono>
#include <vector>

namespace ben_gear::agent {

/// Agent 回调接口
class AgentCallbacks {
public:
    virtual ~AgentCallbacks() = default;
    virtual void on_token(std::string_view /*token*/) {}
    virtual void on_thinking(std::string_view /*token*/) {}
    virtual void on_tool_call(const llm::ToolCallRequest& /*call*/) {}
    virtual void on_tool_result(const llm::ToolCallResult& /*result*/) {}
};

/// 空回调实现
class NullAgentCallbacks : public AgentCallbacks {};

/// Agent 类（使用原生工具调用 API）
class Agent {
public:
    explicit Agent(config::Settings settings)
        : settings_(std::move(settings)),
          provider_(settings_),
          tools_(ToolRegistry()),
          tool_manager_(tools_),
          skill_loader_(skill::make_skill_loader(settings_.workspace)),
          mcp_manager_(settings_.mcp.read_buffer_size),
          max_tool_steps_(settings_.agent.max_tool_steps) {
        // 1. 注册所有工具（内置工具 + 技能工具 + 技能管理工具）
        tools::register_all_tools(tools_, settings_.agent.command_timeout, &skill_loader_);
        // 2. 发现磁盘技能
        skill_loader_.discover();
        // 3. 注册内置技能元数据
        for (auto& def : tools::builtin_skill_definitions()) {
            skill_loader_.add_skill(def);
        }
        // 4. 初始化 MCP 服务器并注册 MCP 工具
        if (!settings_.mcp_servers.empty()) {
            mcp_manager_.load_servers(settings_.mcp_servers);
            for (const auto& tool_def : mcp_manager_.all_tool_definitions()) {
                std::string raw_name(tool_def.name);
                std::string mcp_name = "mcp_" + raw_name;
                tools_.register_tool(
                    base::container::String(mcp_name.c_str()),
                    tool_def.description,
                    tool_def.parameters,
                    [this, raw_name](const Json& args) -> std::string {
                        return mcp_manager_.execute_tool(raw_name, args);
                    }
                );
                log::info_fmt("registered MCP tool: {} -> {}", raw_name, mcp_name);
            }
        }
    }
    
    /// 设置是否启用会话记忆
    void set_enable_memory(bool enable) {
        enable_memory_ = enable;
    }
    
    bool enable_memory() const noexcept {
        return enable_memory_;
    }
    
    /// 清空会话记忆
    void clear_memory() {
        std::lock_guard lock(history_mutex_);
        history_.clear();
    }

    /// 获取会话历史
    llm::ConversationHistory history() const {
        std::shared_lock lock(history_mutex_);
        return history_;
    }
    
    /// 获取工具注册表
    const llm::ToolRegistry& tools() const noexcept {
        return tools_;
    }

    /// 获取技能加载器
    const skill::SkillLoader& skill_loader() const noexcept {
        return skill_loader_;
    }
    
    /// 注册自定义工具
    void register_tool(const base::container::String& name,
                      const base::container::String& description,
                      const base::container::Vector<std::pair<base::container::String, llm::ToolParameterSchema>>& parameters,
                      llm::ToolExecutor executor) {
        tools_.register_tool(name, description, parameters, std::move(executor));
    }
    
    /// 同步聊天
    llm::ChatResult run(base::container::String prompt) {
        NullAgentCallbacks callbacks;
        return run(std::move(prompt), callbacks);
    }

    llm::ChatResult run(base::container::String prompt, AgentCallbacks& callbacks) {
        net::NetworkRuntime runtime;
        net::EventLoop loop;
        auto result = loop.run(run_async(loop, std::move(prompt), callbacks));
        if (result.status >= 200 && result.status < 300) {
            callbacks.on_token(result.text);
        }
        return result;
    }

    /// 异步聊天
    net::Task<llm::ChatResult> run_async(net::EventLoop& loop, base::container::String prompt, AgentCallbacks& callbacks) {
        // 初始化会话历史
        {
            std::lock_guard lock(history_mutex_);
            if (history_.empty()) {
                history_.add_system(base::container::String(system_prompt().c_str()));
            }

            // 添加用户消息
            history_.add_user(prompt);
        }

        // 工具调用循环
        for (int step = 0; step < max_tool_steps_; ++step) {
            // 调用 LLM
            llm::ConversationHistory snapshot;
            {
                std::shared_lock lock(history_mutex_);
                snapshot = history_;
            }
            auto response = co_await provider_.chat_with_tools_async(loop, snapshot, tools_);

            // 检查是否有工具调用
            if (!llm::ToolCallManager::has_tool_calls(response, settings_.provider)) {
                // 没有工具调用，先输出 thinking 再提取正文
                emit_thinking(response, callbacks);
                auto text = extract_response_text(response);
                {
                    std::lock_guard lock(history_mutex_);
                    history_.add_assistant(base::container::String(text.c_str()));
                }
                co_return llm::ChatResult{200, text, response.dump()};
            }

            // 提取并执行工具调用
            auto tool_calls = extract_tool_calls(response);
            for (const auto& call : tool_calls) {
                log::info_fmt("tool call started: name={}, id={}, args={}", call.name, call.id, call.arguments.dump());
            }
            auto results = tool_manager_.execute_tools(tool_calls);
            for (const auto& result : results) {
                log::info_fmt("tool call completed: name={}, success={}, output_size={}",
                              result.name, result.success, result.output.size());
            }

            // 添加助手消息（包含工具调用）
            {
                std::lock_guard lock(history_mutex_);
                add_assistant_message_with_tools(response, tool_calls);

                // 添加工具结果
                for (const auto& result : results) {
                    history_.add_tool_result(result.tool_call_id, result.name, result.output);
                }
            }

            // 回调通知
            for (const auto& result : results) {
                callbacks.on_tool_result(result);
            }
            for (const auto& call : tool_calls) {
                callbacks.on_tool_call(call);
            }
        }

        // 达到工具调用上限
        co_return llm::ChatResult{200, "Tool call limit reached", ""};
    }
    
    /// 流式聊天
    llm::StreamResult run_stream(base::container::String prompt, const llm::StreamTokenHandler& on_token) {
        NullAgentCallbacks callbacks;
        return run_stream(std::move(prompt), on_token, callbacks);
    }

    llm::StreamResult run_stream(base::container::String prompt, const llm::StreamTokenHandler& on_token, AgentCallbacks& callbacks) {
        net::NetworkRuntime runtime;
        net::EventLoop loop;
        auto result = loop.run(run_stream_async(loop, std::move(prompt), on_token, callbacks));
        return result;
    }

    net::Task<llm::StreamResult> run_stream_async(net::EventLoop& loop,
                                                  base::container::String prompt,
                                                  llm::StreamTokenHandler on_token,
                                                  AgentCallbacks& callbacks) {
        // 初始化会话历史
        {
            std::lock_guard lock(history_mutex_);
            if (history_.empty()) {
                history_.add_system(base::container::String(system_prompt().c_str()));
            }

            // 添加用户消息
            history_.add_user(std::move(prompt));
        }

        // 工具调用循环
        for (int step = 0; step < max_tool_steps_; ++step) {
            {
                std::shared_lock lock(history_mutex_);
                log::info_fmt("agent loop step={} history_size={}", step, history_.size());
            }

            // 积累正文和工具调用，支持重试时清空重来
            base::container::String accumulated_text;
            struct PendingToolCall {
                base::container::String id;
                base::container::String name;
                base::container::String arguments;
            };
            std::map<int, PendingToolCall> pending_tools;

            llm::StreamHandlers handlers;
            handlers.on_token = [&](std::string_view token) {
                callbacks.on_token(token);
                accumulated_text += token;
                if (on_token) {
                    on_token(token);
                }
            };
            handlers.on_thinking = [&](std::string_view token) {
                callbacks.on_thinking(token);
            };
            handlers.on_tool_call = [&](const llm::StreamToolCallDelta& delta) {
                auto& tc = pending_tools[delta.index];
                if (!delta.id.empty()) tc.id = delta.id;
                if (!delta.name.empty()) tc.name = delta.name;
                tc.arguments += delta.arguments;
            };

            // 调用带工具的流式 API（含重试）
            auto& retry_cfg = settings_.llm_request_retry;
            llm::StreamResult result;
            for (int attempt = 1; attempt <= retry_cfg.max_attempts; ++attempt) {
                accumulated_text.clear();
                pending_tools.clear();
                llm::ConversationHistory snapshot;
                {
                    std::shared_lock lock(history_mutex_);
                    snapshot = history_;
                }
                result = co_await provider_.chat_stream_with_tools_async(
                    loop, snapshot, tools_, {}, handlers);

                if (result.status >= 200 && result.status < 300) {
                    if (attempt > 1) {
                        log::info_fmt("agent stream succeeded on attempt={}", attempt);
                    }
                    break;
                }

                bool retryable = (result.status == 429 || result.status >= 500);
                if (!retryable || attempt == retry_cfg.max_attempts) {
                    log::error_fmt("agent stream failed status={} attempt={}/{}", result.status, attempt, retry_cfg.max_attempts);
                    break;
                }

                auto delay = retry_cfg.initial_delay_ms * (1 << (attempt - 1));
                if (delay > retry_cfg.max_delay_ms) delay = retry_cfg.max_delay_ms;
                log::warn_fmt("agent stream retryable status={} attempt={}/{} retry_in={}ms",
                              result.status, attempt, retry_cfg.max_attempts, delay);
                // 关闭 thinking 标记再重试
                callbacks.on_token("");
                co_await loop.sleep_for(std::chrono::milliseconds(delay));
            }

            log::info_fmt("agent loop step={} stream_done status={} text_len={} tool_calls={}",
                          step, result.status, accumulated_text.size(), pending_tools.size());

            // 关闭未关闭的 thinking 标记
            callbacks.on_token("");

            // 没有工具调用，直接返回
            if (pending_tools.empty()) {
                log::info_fmt("agent loop done: no tool calls, text_len={}", accumulated_text.size());
                {
                    std::lock_guard lock(history_mutex_);
                    history_.add_assistant(std::move(accumulated_text));
                }
                co_return result;
            }

            // 有工具调用，构建 ToolCallRequest 并执行
            std::vector<llm::ToolCallRequest> tool_calls;
            for (auto& [idx, tc] : pending_tools) {
                log::info_fmt("tool_call[{}]: id={} name={} args_len={}", idx, tc.id, tc.name, tc.arguments.size());
                llm::ToolCallRequest req;
                req.id = std::move(tc.id);
                req.name = std::move(tc.name);
                std::string err;
                req.arguments = parse_json(std::string(tc.arguments.data(), tc.arguments.size()), err);
                if (!err.empty()) {
                    log::error_fmt("stream tool call arguments parse failed: name={} error={}", tc.name, err);
                    req.arguments = Json::object({{"_parse_error", err}, {"_raw_arguments", std::string(tc.arguments.data(), tc.arguments.size())}});
                }
                tool_calls.push_back(std::move(req));
            }

            auto tool_exec_start = std::chrono::steady_clock::now();
            for (const auto& call : tool_calls) {
                log::info_fmt("tool call started: name={}, id={}, args={}", call.name, call.id, call.arguments.dump());
            }
            auto tool_results = tool_manager_.execute_tools(tool_calls);
            auto tool_exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tool_exec_start).count();
            log::info_fmt("tool execution done: {} calls in {}ms", tool_calls.size(), tool_exec_ms);

            // 添加助手消息（包含工具调用）+ 工具结果
            {
                std::lock_guard lock(history_mutex_);
                add_stream_assistant_message(accumulated_text, tool_calls);

                for (const auto& tr : tool_results) {
                    log::info_fmt("tool_result: id={} name={} success={} output_len={}",
                                  tr.tool_call_id, tr.name,
                                  tr.success, tr.output.size());
                    history_.add_tool_result(tr.tool_call_id, tr.name, tr.output);
                }
            }

            // 回调通知
            for (const auto& call : tool_calls) {
                callbacks.on_tool_call(call);
            }
            for (const auto& tr : tool_results) {
                callbacks.on_tool_result(tr);
            }

            // 继续下一轮循环
        }

        log::warn_fmt("agent loop: max_tool_steps={} reached", max_tool_steps_);
        co_return llm::StreamResult{200, ""};
    }
    
    /// 获取配置
    const config::Settings& settings() const noexcept {
        return settings_;
    }

private:
    /// 系统提示（分层组装）
    std::string system_prompt() const {
        std::string prompt;
        if (!settings_.agent.system_prompt.empty()) {
            prompt = settings_.agent.system_prompt + "\n\n";
        } else {
            prompt =
                "You are BenGear, a concise cross-platform coding agent. "
                "Prefer direct, actionable answers and avoid unnecessary dependencies.\n\n";
        }

        // 注入技能元数据（Level 1 渐进式披露）
        auto skills_meta = skill_loader_.get_skills_metadata();
        if (!skills_meta.empty()) {
            prompt += skills_meta.c_str();
            prompt += "\nTo use a skill, call the get_skill tool with the skill name. "
                      "This loads detailed instructions into the conversation.\n";
        }

        return prompt;
    }
    
    /// 从响应中提取 thinking 并通知回调
    void emit_thinking(const Json& response, AgentCallbacks& callbacks) const {
        if (settings_.provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                const auto& message = response["choices"][0]["message"];
                if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
                    callbacks.on_thinking(message["reasoning_content"].get<std::string>());
                }
            }
        } else {  // anthropic
            if (response.contains("content") && response["content"].is_array()) {
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "thinking") {
                        if (block.contains("thinking") && !block["thinking"].is_null()) {
                            callbacks.on_thinking(block["thinking"].get<std::string>());
                        }
                    }
                }
            }
        }
    }

    /// 从响应中提取文本（排除 thinking）
    std::string extract_response_text(const Json& response) const {
        if (settings_.provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                const auto& message = response["choices"][0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
        } else {  // anthropic
            if (response.contains("content") && response["content"].is_array() && !response["content"].empty()) {
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        return block.value("text", "");
                    }
                }
            }
        }
        return "";
    }
    
    /// 从响应中提取工具调用
    std::vector<llm::ToolCallRequest> extract_tool_calls(const Json& response) const {
        if (settings_.provider == config::Provider::openai) {
            return tool_manager_.extract_openai_tool_calls(response);
        } else {
            return tool_manager_.extract_anthropic_tool_calls(response);
        }
    }
    
    /// 添加助手消息（包含工具调用）
    void add_assistant_message_with_tools(const Json& response, const std::vector<llm::ToolCallRequest>& calls) {
        if (settings_.provider == config::Provider::openai) {
            // OpenAI: 添加包含 tool_calls 的助手消息
            Json msg = response["choices"][0]["message"];
            llm::Message agent_msg;
            agent_msg.role = llm::MessageRole::assistant;
            agent_msg.content = base::container::String(msg.value("content", "").c_str());
            agent_msg.blocks = convert_tool_calls_to_blocks(calls);
            history_.add_message(agent_msg);
        } else {
            // Anthropic: 添加包含 tool_use 的助手消息
            llm::Message msg;
            msg.role = llm::MessageRole::assistant;
            
            // 提取文本块
            if (response.contains("content") && response["content"].is_array()) {
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        msg.content = base::container::String(block.value("text", "").c_str());
                        break;
                    }
                }
            }
            
            // 添加工具调用块
            msg.blocks = convert_tool_calls_to_blocks(calls);
            
            history_.add_message(msg);
        }
    }
    
    /// 转换工具调用为内容块
    base::container::Vector<llm::ContentBlock> convert_tool_calls_to_blocks(const std::vector<llm::ToolCallRequest>& calls) {
        base::container::Vector<llm::ContentBlock> blocks;
        for (const auto& call : calls) {
            blocks.push_back(llm::ContentBlock::tool_use_block(call));
        }
        return blocks;
    }

    /// 从流式响应添加助手消息（包含工具调用）
    void add_stream_assistant_message(const base::container::String& text, const std::vector<llm::ToolCallRequest>& calls) {
        llm::Message msg;
        msg.role = llm::MessageRole::assistant;
        msg.content = text;
        msg.blocks = convert_tool_calls_to_blocks(calls);
        history_.add_message(msg);
    }

    config::Settings settings_;
    llm::ProviderClient provider_;
    llm::ToolRegistry tools_;
    llm::ToolCallManager tool_manager_;
    skill::SkillLoader skill_loader_;
    mcp::MCPManager mcp_manager_;
    llm::ConversationHistory history_;
    mutable std::shared_mutex history_mutex_;

    bool enable_memory_ = true;
    int max_tool_steps_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using Agent = agent::Agent;
using AgentCallbacks = agent::AgentCallbacks;
}  // namespace ben_gear
