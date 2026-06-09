#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/memory/compactor.hpp"
#include "ben_gear/memory/updater.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/tools/memory_tools.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

/// 会话类 — 隔离单元
/// 每个 Session 独占 ConversationHistory、Compactor、MemoryUpdater、EpisodeStore
/// EventLoop 由 SharedResources 的 IoContext 统一管理，Session 不持有
/// 多个 Session 之间不共享可变状态，无需加锁
class Session {
public:
    /// 构造会话，依赖通过 SessionDeps 注入
    /// tools 参数：由 SharedResources 持有的工具注册表，Session 在其上追加情景工具
    /// 注意：tools 必须有效，且生命周期长于 Session（由 SharedResources 保证）
    /// 
    /// 线程安全说明：
    /// - Session 构造时会修改 ToolRegistry（注册情景记忆工具）
    /// - 调用方需确保同一时刻只有一个 Session 在构造（由 Agent 保证）
    explicit Session(SessionConfig config, SessionDeps deps,
                     llm::ToolRegistry& tools)
        : session_id_(config.session_id.empty() ? ::ben_gear::workspace::generate_uuid() : config.session_id),
          ws_ctx_(deps.ws_ctx),
          memory_store_(deps.memory_store)
    {
        // 创建会话目录
        session_dir_ = ws_ctx_.tier_paths.workspace_dir / "sessions"
                       / std::string(session_id_.data(), session_id_.size());
        std::filesystem::create_directories(session_dir_);
        std::filesystem::create_directories(session_dir_ / "memory");


        // 创建会话级 EpisodeStore（绑定到 session_dir）
        episode_store_ = std::make_shared<memory::EpisodeStore>(session_dir_);

        // 注册情景记忆工具到工具注册表
        tools::register_episode_tools(tools, episode_store_);

        // 创建会话级 Compactor 和 MemoryUpdater
        memory::Compactor::Config compactor_cfg;
        compactor_cfg.context_length = config.context_length;
        compactor_ = std::make_unique<memory::Compactor>(
            compactor_cfg, *memory_store_, *episode_store_,
            *deps.context_builder,
            ws_ctx_.tier_paths.workspace_dir / "memory");
        memory_updater_ = std::make_unique<memory::MemoryUpdater>(
            *memory_store_, *episode_store_,
            ws_ctx_.tier_paths.workspace_dir / "sessions");

        log::info_fmt("session created: id={}", std::string(session_id_.data(), session_id_.size()));
    }

    /// 独占资源
    workspace::ConversationHistory& history() { return history_; }
    const workspace::ConversationHistory& history() const { return history_; }

    /// 元数据
    const container::String& session_id() const { return session_id_; }
    const WorkspaceContext& workspace_context() const { return ws_ctx_; }
    const std::filesystem::path& session_dir() const { return session_dir_; }
    memory::MemoryStore& memory_store() { return *memory_store_; }
    const memory::MemoryStore& memory_store() const { return *memory_store_; }
    const std::shared_ptr<memory::EpisodeStore>& episode_store() const { return episode_store_; }

    /// 压缩检查（会话级状态，独占）
    /// 注意：此方法较长，未来可拆分为 check_compaction / do_compact / update_memory
    void maybe_compact(net::EventLoop& loop,
                       const llm::ProviderClient& provider,
                       const llm::ToolRegistry& tools) {
        if (!compactor_ || !compactor_->should_compact_local(history_)) return;

        auto chat_fn = [&loop, &provider, &tools](const std::string& prompt) -> std::string {
            workspace::ConversationHistory tmp;
            tmp.add_user(container::String(prompt.c_str()));
            auto response = net::sync_wait(loop, provider.chat_with_tools_async(loop, tmp, tools));
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                Json choices = response["choices"]; Json message = choices[0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
            if (response.contains("content") && response["content"].is_array()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        return block.value("text", "");
                    }
                }
            }
            return "";
        };

        compactor_->compact(history_, chat_fn);

        // 统一重建缓存（保守策略，确保正确性）
        history_.invalidate_cache();

        // 传 round summaries（用户+助手配对），而非仅 assistant
        if (memory_updater_) {
            container::Vector<container::String> summaries;
            auto& msgs = history_.messages();
            for (size_t i = 0; i < msgs.size(); ++i) {
                auto& msg = msgs[i];
                if (msg.role() == acp::Role::User) {
                    auto user_text = msg.get_all_text();
                    auto user_content = std::string(user_text.data(), user_text.size());
                    if (user_content.size() > 100) user_content = user_content.substr(0, 100) + "...";
                    std::string assistant_content;
                    for (size_t j = i + 1; j < msgs.size(); ++j) {
                        if (msgs[j].role() == acp::Role::Assistant) {
                            auto assistant_text = msgs[j].get_all_text();
                            assistant_content = std::string(assistant_text.data(), assistant_text.size());
                            if (assistant_content.size() > 200) {
                                assistant_content = assistant_content.substr(0, 200) + "...";
                            }
                            break;
                        }
                    }
                    if (!assistant_content.empty()) {
                        std::string summary = "用户: " + user_content + "\n助手: " + assistant_content;
                        summaries.push_back(container::String(summary.c_str()));
                    }
                }
            }
            if (!summaries.empty()) {
                memory_updater_->update(summaries, chat_fn);
            }
        }
        log::info_fmt("session compacted: history_size={}", history_.size());
    }

    /// 持久化用户消息
    void persist_user_message(const container::String& content,
                              workspace::HistoryDB& db) {
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            container::String("user"),
            content,
            container::String()
        );
    }

    /// 通用消息持久化
    void persist_message(const container::String& role,
                         const container::String& content,
                         workspace::HistoryDB& db) {
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            role,
            content,
            container::String()
        );
    }

    /// 持久化 assistant 消息（包含 tool_calls 元数据）
    void persist_assistant_message(const container::String& content,
                                   const std::vector<llm::ToolCallRequest>& tool_calls,
                                   workspace::HistoryDB& db) {
        Json metadata;
        Json tool_calls_arr = Json::array();
        for (auto call : tool_calls) {
            tool_calls_arr.push_back({
                {"id", std::string(call.id.data(), call.id.size())},
                {"name", std::string(call.name.data(), call.name.size())},
                {"input", call.arguments}
            });
        }
        metadata["tool_calls"] = tool_calls_arr;
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            container::String("assistant"),
            content,
            container::String(metadata.dump().c_str())
        );
    }

    /// 持久化带工具调用的 assistant 消息
    void persist_assistant_with_tools(const container::String& content,
                                      const std::vector<llm::ToolCallRequest>& tool_calls,
                                      workspace::HistoryDB& db) {
        persist_assistant_message(content, tool_calls, db);
    }

    /// 持久化工具结果
    void persist_tool_result(const container::String& tool_call_id,
                             const container::String& tool_name,
                             const container::String& content,
                             workspace::HistoryDB& db) {
        Json metadata;
        metadata["tool_call_id"] = std::string(tool_call_id.data(), tool_call_id.size());
        metadata["tool_name"] = std::string(tool_name.data(), tool_name.size());
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            container::String("tool"),
            content,
            container::String(metadata.dump().c_str())
        );
    }

    /// 恢复会话历史
    void restore_from_db(workspace::HistoryDB& db) {
        auto messages = db.load_session(ws_ctx_.workspace_name, session_id_);

        struct ParsedMsg {
            std::string role;
            container::String content;
            Json metadata;
        };
        std::vector<ParsedMsg> parsed;
        parsed.reserve(messages.size());

        for (auto msg : messages) {
            auto role = msg.value("role", "");
            auto content = container::String(msg.value("content", "").c_str());
            Json metadata;
            if (msg.contains("metadata") && msg["metadata"].is_string()) {
                std::string meta_err;
                metadata = parse_json(msg["metadata"].get<std::string>(), meta_err);
            }
            parsed.push_back({role, std::move(content), std::move(metadata)});
        }

        for (size_t i = 0; i < parsed.size(); ++i) {
            auto msg = parsed[i];

            if (msg.role == "system") {
                continue;
            }

            if (msg.role == "user") {
                history_.add_user(msg.content);
                continue;
            }

            if (msg.role == "assistant") {
                // 创建 assistant 消息
                auto acp_msg = acp::ACPMessage::assistant_message(msg.content);

                // 添加工具调用
                if (msg.metadata.is_object() && msg.metadata.contains("tool_calls")) {
                    for (auto tc : msg.metadata["tool_calls"]) {
                        llm::ToolCallRequest call;
                        call.id = container::String(tc.value("id", "").c_str());
                        call.name = container::String(tc.value("name", "").c_str());
                        call.arguments = tc.value("input", Json::object());
                        acp_msg.add_tool_use(call);
                    }
                }

                history_.add_message(acp_msg);
                continue;
            }

            if (msg.role == "tool") {
                container::String tool_call_id;
                container::String tool_name;
                if (msg.metadata.is_object()) {
                    if (msg.metadata.contains("tool_call_id") && msg.metadata["tool_call_id"].is_string()) {
                        tool_call_id = msg.metadata["tool_call_id"].get<container::String>();
                    }
                    if (msg.metadata.contains("tool_name") && msg.metadata["tool_name"].is_string()) {
                        tool_name = msg.metadata["tool_name"].get<container::String>();
                    }
                }
                if (!tool_call_id.empty() && !tool_name.empty()) {
                    history_.add_tool_result(tool_call_id, tool_name, msg.content);
                }
            }
        }

        log::info_fmt("session restored: id={}, messages={}",
                      std::string(session_id_.data(), session_id_.size()),
                      messages.size());
    }

private:

    container::String session_id_;
    WorkspaceContext ws_ctx_;
    std::filesystem::path session_dir_;

    // 独占资源（每个 Session 一份，不共享）
    workspace::ConversationHistory history_;
    std::unique_ptr<memory::Compactor> compactor_;
    std::unique_ptr<memory::MemoryUpdater> memory_updater_;
    std::shared_ptr<memory::EpisodeStore> episode_store_;

    // 共享资源（通过 shared_ptr 共享所有权）
    std::shared_ptr<memory::MemoryStore> memory_store_;
};

}  // namespace ben_gear::workspace
