#include "ben_gear/workspace/session.hpp"

namespace ben_gear::workspace {

Session::Session(SessionConfig config, SessionDeps deps,
                 llm::ToolRegistry& tools)
    : session_id_(config.session_id.empty()
                      ? ::ben_gear::workspace::generate_uuid()
                      : config.session_id),
      ws_ctx_(deps.ws_ctx),
      memory_store_(deps.memory_store) {
    // 创建会话目录
    session_dir_ = ws_ctx_.tier_paths.workspace_dir / "sessions" /
                   std::string(session_id_.data(), session_id_.size());
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

    log::info_fmt("session created: id={}",
                  std::string(session_id_.data(), session_id_.size()));
}

void Session::maybe_compact(net::EventLoop& loop,
                            llm::ProviderClient& provider,
                            const llm::ToolRegistry& tools) {
    if (!compactor_ || !compactor_->should_compact_local(history_)) return;

    auto chat_fn = [&loop, &provider,
                    &tools](const std::string& prompt) -> std::string {
        workspace::ConversationHistory tmp;
        tmp.add_user(container::String(prompt.c_str()));
        auto response = net::sync_wait(
            loop, provider.chat_with_tools_async(loop, tmp, tools));
        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty()) {
            Json choices = response["choices"];
            Json message = choices[0]["message"];
            if (message.contains("content") &&
                !message["content"].is_null()) {
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
                auto user_content =
                    std::string(user_text.data(), user_text.size());
                if (user_content.size() > 100)
                    user_content = user_content.substr(0, 100) + "...";
                std::string assistant_content;
                for (size_t j = i + 1; j < msgs.size(); ++j) {
                    if (msgs[j].role() == acp::Role::Assistant) {
                        auto assistant_text = msgs[j].get_all_text();
                        assistant_content =
                            std::string(assistant_text.data(),
                                        assistant_text.size());
                        if (assistant_content.size() > 200) {
                            assistant_content =
                                assistant_content.substr(0, 200) + "...";
                        }
                        break;
                    }
                }
                if (!assistant_content.empty()) {
                    std::string summary =
                        "用户: " + user_content + "\n助手: " + assistant_content;
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

void Session::persist_user_message(const container::String& content,
                                   workspace::HistoryDB& db) {
    db.append(ws_ctx_.workspace_name, session_id_, container::String("user"),
              content);
}

void Session::persist_message(const container::String& role,
                              const container::String& content,
                              workspace::HistoryDB& db) {
    db.append(ws_ctx_.workspace_name, session_id_, role, content);
}

void Session::persist_assistant_message(
    const container::String& content,
    const std::vector<llm::ToolCallRequest>& tool_calls,
    workspace::HistoryDB& db) {
    db.append(ws_ctx_.workspace_name, session_id_,
              container::String("assistant"), content);
    for (const auto& call : tool_calls) {
        auto args_str = call.arguments.dump();
        db.append(ws_ctx_.workspace_name, session_id_,
                  container::String("tool_call"),
                  container::String(args_str.c_str()), call.id, call.name);
    }
}

void Session::persist_assistant_with_tools(
    const container::String& content,
    const std::vector<llm::ToolCallRequest>& tool_calls,
    workspace::HistoryDB& db) {
    persist_assistant_message(content, tool_calls, db);
}

void Session::persist_tool_result(const container::String& tool_call_id,
                                  const container::String& tool_name,
                                  const container::String& content,
                                  workspace::HistoryDB& db) {
    db.append(ws_ctx_.workspace_name, session_id_,
              container::String("tool"), content, tool_call_id, tool_name);
}

void Session::restore_from_db(workspace::HistoryDB& db) {
    db.flush();
    auto messages =
        db.load_session(ws_ctx_.workspace_name, session_id_);

    for (size_t i = 0; i < messages.size(); ++i) {
        auto role = messages[i].value("role", "");
        auto content =
            container::String(messages[i].value("content", "").c_str());

        if (role == "system" || role == "thinking") continue;

        if (role == "user") {
            history_.add_user(content);
            continue;
        }

        if (role == "assistant") {
            auto acp_msg = acp::ACPMessage::assistant_message(content);
            for (size_t j = i + 1; j < messages.size(); ++j) {
                if (messages[j].value("role", "") != "tool_call") break;
                llm::ToolCallRequest call;
                call.id = container::String(
                    messages[j].value("tool_call_id", "").c_str());
                call.name = container::String(
                    messages[j].value("tool_name", "").c_str());
                auto args_str = messages[j].value("content", "");
                if (!args_str.empty()) {
                    try {
                        call.arguments = Json::parse(args_str);
                    } catch (...) {
                    }
                }
                acp_msg.add_tool_use(call);
            }
            history_.add_message(acp_msg);
            continue;
        }

        if (role == "tool_call") {
            continue;
        }

        if (role == "tool") {
            auto tc_id = container::String(
                messages[i].value("tool_call_id", "").c_str());
            auto tc_name = container::String(
                messages[i].value("tool_name", "").c_str());
            if (!tc_id.empty() && !tc_name.empty()) {
                history_.add_tool_result(tc_id, tc_name, content);
            }
        }
    }

    log::info_fmt("session restored: id={}, messages={}",
                  std::string(session_id_.data(), session_id_.size()),
                  messages.size());
}

}  // namespace ben_gear::workspace
