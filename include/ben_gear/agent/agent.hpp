#pragma once

#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/tool/manager.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <chrono>
#include <vector>

namespace ben_gear::agent {

namespace container = base::container;

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

/// Agent 类 — 无状态调度器
/// 不持有 ConversationHistory，run_async 接受 Session 引用
/// 共享只读资源通过 SharedResources 管理，多 Agent/多会话可复用
class Agent {
public:
    /// 从 SharedResources 构造（支持多 Agent 共享资源）
    Agent(std::shared_ptr<SharedResources> resources)
        : resources_(std::move(resources)),
          tool_manager_(resources_->tools(), resources_->core_pool(),
                        std::chrono::seconds(resources_->settings().agent.command_timeout),
                          resources_),
          enable_memory_(true) {
    }

    /// 从 Settings + WorkspaceContext 构造（内部创建 SharedResources）
    Agent(config::Settings settings, workspace::WorkspaceContext ws_ctx)
        : resources_(std::make_shared<SharedResources>(std::move(settings), std::move(ws_ctx))),
          tool_manager_(resources_->tools(), resources_->core_pool(),
                        std::chrono::seconds(resources_->settings().agent.command_timeout),
                          resources_),
          enable_memory_(true) {
        resources_->post_init();  // 注册需要 shared_from_this 的工具（工作流）
    }

    /// 设置是否启用会话记忆
    void set_enable_memory(bool enable) {
        enable_memory_.store(enable, std::memory_order_relaxed);
    }

    bool enable_memory() const noexcept {
        return enable_memory_.load(std::memory_order_relaxed);
    }

    /// 基于 Session 的异步聊天（线程安全，Session 独占 history）
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  base::container::String prompt,
                                                  const AgentCallbacks& callbacks,
                                                  const net::CancellationToken& cancel = {}) {
        auto& history = session.history();

        log::info_fmt("agent session started: session_id={}, stream={}, prompt_len={}",
            std::string(session.session_id().data(), session.session_id().size()),
            resources_->settings().stream, prompt.size());
        if (history.empty()) {
            history.add_system(base::container::String(system_prompt().c_str()));
        }
        auto prompt_copy = container::String(prompt.c_str());
        history.add_user(std::move(prompt));

        if (resources_->settings().stream) {
            co_return co_await run_session_stream_step(loop, session, history, prompt_copy, callbacks, cancel);
        }

        // 非流式路径（重试已在 with_http_retry_async 内部处理）
        for (int step = 0; step < resources_->max_tool_steps(); ++step) {
            cancel.throw_if_cancelled();
            log::info_fmt("agent non-stream step {}/{}: sending request", step + 1, resources_->max_tool_steps());
            auto response = co_await resources_->provider().chat_with_tools_async(loop, history, resources_->tools());

            // 检查是否为有效的 LLM 响应
            bool has_content = false;
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                has_content = true;
            } else if (response.contains("content") && response["content"].is_array()) {
                has_content = true;
            }
            if (!has_content) {
                // HTTP 错误或无效响应，提取错误信息
                std::string error_msg = response.dump();
                if (response.contains("error") && response["error"].is_object()) {
                    error_msg = response["error"].value("message", response.dump());
                }
                int status = 0;
                if (response.contains("error") && response["error"].contains("status")) {
                    status = response["error"]["status"].get<int>();
                }
                log::error_fmt("agent non-stream invalid response: status={}", status);
                co_return llm::ChatResult{status > 0 ? status : 500, {}, response.dump(), container::String(error_msg.c_str())};
            }

            if (!llm::ToolCallManager::has_tool_calls(response, resources_->settings().provider)) {
                emit_thinking(response, callbacks);
                auto text = extract_response_text(response);
                history.add_assistant(base::container::String(text.c_str()));
                callbacks.on_token(text);

                session.persist_message(container::String("user"), prompt_copy, resources_->history_db());
                session.persist_message(container::String("assistant"), base::container::String(text.c_str()), resources_->history_db());

                session.maybe_compact(loop, resources_->provider(), resources_->tools());

                co_return llm::ChatResult{200, text, response.dump(), {}};
            }

            auto tool_calls = extract_tool_calls(response);
            for (const auto& call : tool_calls) {
                log::info_fmt("tool call started: name={}, id={}, args={}", call.name, call.id, call.arguments.dump());
            }

            // 设置工作流命名空间（username::workspace::session_id），工具自动读取
            workflow::WorkflowEngine::set_current_namespace(
                std::string(resources_->workspace_context().username.data(),
                            resources_->workspace_context().username.size()) + "::" +
                std::string(resources_->workspace_context().workspace_name.data(),
                            resources_->workspace_context().workspace_name.size()) + "::" +
                std::string(session.session_id().data(),
                            session.session_id().size()));
            auto results = tool_manager_.execute_tools(tool_calls);
            workflow::WorkflowEngine::clear_current_namespace();
            for (const auto& result : results) {
                log::info_fmt("tool call completed: name={}, success={}, output_size={}",
                              result.name, result.success, result.output.size());
            }

            add_assistant_message_with_tools_to(response, tool_calls, history);
            persist_tool_step(session, history, tool_calls, results);

            for (const auto& result : results) { callbacks.on_tool_result(result); }
            for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
        }

        co_return llm::ChatResult{200, "Tool call limit reached", "", {}};
    }

    /// 获取共享资源
    std::shared_ptr<SharedResources> resources() const noexcept { return resources_; }

    /// 获取配置
    const config::Settings& settings() const noexcept { return resources_->settings(); }

    /// 获取工具注册表
    const llm::ToolRegistry& tools() const noexcept { return resources_->tools(); }

    /// 获取技能加载器
    const skill::SkillLoader& skill_loader() const noexcept { return resources_->skill_loader(); }

    /// 获取记忆存储
    const memory::MemoryStore& memory_store() const noexcept { return *resources_->memory_store(); }

    /// 获取历史数据库
    session::HistoryDB& history_db() noexcept { return resources_->history_db(); }

    /// 获取工作空间上下文
    const workspace::WorkspaceContext& workspace_context() const noexcept { return resources_->workspace_context(); }

    /// 获取工作空间管理器
    workspace::WorkspaceManager& workspace_manager() noexcept { return *resources_->workspace_manager(); }

    /// 注册自定义工具
    void register_tool(const base::container::String& name,
                      const base::container::String& description,
                      const base::container::Vector<std::pair<base::container::String, llm::ToolParameterSchema>>& parameters,
                      llm::ToolExecutor executor) {
        resources_->register_tool(name, description, parameters, std::move(executor));
    }

private:
    /// 系统提示（分层组装）
    std::string system_prompt() const {
        if (resources_->context_builder()) {
            return resources_->context_builder()->build();
        }
        
        std::string prompt;
        const auto& sp = resources_->settings().agent.system_prompt;
        
        // 预估大小，避免多次 realloc
        size_t estimated_size = 256;  // 基础提示词大小
        if (!sp.empty()) {
            estimated_size += sp.size() + 2;  // +2 for "\n\n"
        }
        auto skills_meta = resources_->skill_loader().get_skills_metadata();
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

    void emit_thinking(const Json& response, const AgentCallbacks& callbacks) const {
        if (resources_->settings().provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                const auto& message = response["choices"][0]["message"];
                if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
                    callbacks.on_thinking(message["reasoning_content"].get<std::string>());
                }
            }
        } else {
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

    std::string extract_response_text(const Json& response) const {
        if (resources_->settings().provider == config::Provider::openai) {
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                const auto& message = response["choices"][0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
        } else {
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

    std::vector<llm::ToolCallRequest> extract_tool_calls(const Json& response) const {
        if (resources_->settings().provider == config::Provider::openai) {
            return tool_manager_.extract_openai_tool_calls(response);
        } else {
            return tool_manager_.extract_anthropic_tool_calls(response);
        }
    }

    /// 添加带工具调用的助手消息到指定 history（Session 模式）
    void add_assistant_message_with_tools_to(const Json& response,
                                              const std::vector<llm::ToolCallRequest>& calls,
                                              llm::ConversationHistory& history) {
        if (resources_->settings().provider == config::Provider::openai) {
            Json msg = response["choices"][0]["message"];
            llm::Message agent_msg;
            agent_msg.role = llm::MessageRole::assistant;
            agent_msg.content = base::container::String(msg.value("content", "").c_str());
            agent_msg.blocks = convert_tool_calls_to_blocks(calls);
            history.add_message(agent_msg);
        } else {
            llm::Message msg;
            msg.role = llm::MessageRole::assistant;
            if (response.contains("content") && response["content"].is_array()) {
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        msg.content = base::container::String(block.value("text", "").c_str());
                        break;
                    }
                }
            }
            msg.blocks = convert_tool_calls_to_blocks(calls);
            history.add_message(msg);
        }
    }

    /// 流式步骤循环
    net::Task<llm::ChatResult> run_session_stream_step(
            net::EventLoop& loop, workspace::Session& session,
            llm::ConversationHistory& history,
            const container::String& prompt_text,
            const AgentCallbacks& callbacks,
            const net::CancellationToken& cancel) {
        for (int step = 0; step < resources_->max_tool_steps(); ++step) {
            cancel.throw_if_cancelled();
            log::info_fmt("agent stream step {}/{}: sending request", step + 1, resources_->max_tool_steps());
            container::String accumulated_text;
            struct PendingToolCall {
                container::String id;
                container::String name;
                container::String arguments;
            };
            std::map<int, PendingToolCall> pending_tools;

            llm::StreamHandlers handlers;
            handlers.on_token = [&](std::string_view token) {
                callbacks.on_token(token);
                accumulated_text += token;
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

            auto result = co_await resources_->provider().chat_stream_with_tools_async(
                loop, history, resources_->tools(), {}, handlers);

            callbacks.on_token("");

            // 检查流式请求状态
            if (result.status < 200 || result.status >= 300) {
                log::error_fmt("agent stream failed status={}", result.status);
                co_return llm::ChatResult{result.status, {},
                    std::string(result.raw.data(), result.raw.size()), {}};
            }

            if (pending_tools.empty()) {
                log::info_fmt("agent stream done: no tool calls, text_len={}", accumulated_text.size());
                auto text = std::string(accumulated_text.data(), accumulated_text.size());
                history.add_assistant(std::move(accumulated_text));

                session.persist_message(container::String("user"),
                    prompt_text, resources_->history_db());
                session.persist_message(container::String("assistant"),
                    container::String(text.c_str()), resources_->history_db());

                session.maybe_compact(loop, resources_->provider(), resources_->tools());

                co_return llm::ChatResult{200, text, std::string(result.raw.data(), result.raw.size()), {}};
            }

            // 解析工具调用
            std::vector<llm::ToolCallRequest> tool_calls;
            for (auto& [idx, tc] : pending_tools) {
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

            // 设置工作流命名空间（username::workspace::session_id），工具自动读取
            workflow::WorkflowEngine::set_current_namespace(
                std::string(resources_->workspace_context().username.data(),
                            resources_->workspace_context().username.size()) + "::" +
                std::string(resources_->workspace_context().workspace_name.data(),
                            resources_->workspace_context().workspace_name.size()) + "::" +
                std::string(session.session_id().data(),
                            session.session_id().size()));
            auto tool_results = tool_manager_.execute_tools(tool_calls);
            workflow::WorkflowEngine::clear_current_namespace();

            log::info_fmt("agent stream step {}: {} tool calls executed, {} success",
                          step + 1, tool_results.size(),
                          std::count_if(tool_results.begin(), tool_results.end(),
                                       [](const auto& r) { return r.success; }));

            // 更新 history
            llm::Message assistant_msg;
            assistant_msg.role = llm::MessageRole::assistant;
            assistant_msg.content = accumulated_text;
            assistant_msg.blocks = convert_tool_calls_to_blocks(tool_calls);
            history.add_message(assistant_msg);

            for (const auto& tr : tool_results) {
                history.add_tool_result(tr.tool_call_id, tr.name, tr.output);
            }

            // 持久化
            session.persist_assistant_with_tools(assistant_msg.content, tool_calls, resources_->history_db());
            for (const auto& tr : tool_results) {
                session.persist_tool_result(tr.tool_call_id, tr.name, tr.output, resources_->history_db());
            }

            for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
            for (const auto& tr : tool_results) { callbacks.on_tool_result(tr); }
        }

        co_return llm::ChatResult{200, "Tool call limit reached", "", {}};
    }

    /// 持久化工具步骤
    void persist_tool_step(workspace::Session& session,
                           llm::ConversationHistory& history,
                           const std::vector<llm::ToolCallRequest>& calls,
                           const std::vector<llm::ToolCallResult>& results) {
        auto& last_msg = history.messages()[history.size() - 1];
        session.persist_assistant_with_tools(last_msg.content, calls, resources_->history_db());
        for (const auto& r : results) {
            session.persist_tool_result(r.tool_call_id, r.name, r.output, resources_->history_db());
        }
    }

    base::container::Vector<llm::ContentBlock> convert_tool_calls_to_blocks(const std::vector<llm::ToolCallRequest>& calls) {
        base::container::Vector<llm::ContentBlock> blocks;
        for (const auto& call : calls) {
            blocks.push_back(llm::ContentBlock::tool_use_block(call));
        }
        return blocks;
    }

    // 共享资源（一次构建，多 Agent/多会话复用）
    std::shared_ptr<SharedResources> resources_;

    // Per-Agent 状态
    llm::ToolCallManager tool_manager_;
    std::atomic<bool> enable_memory_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using Agent = agent::Agent;
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
using SharedResources = agent::SharedResources;
}  // namespace ben_gear
