#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/log/logger.hpp"

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

    // 设置上下文裁剪配置
    history_.set_prune_config(config.context_prune);
    log::info_fmt("session context_prune: enabled={}, protect_recent={}, soft_lines={}, hard_after={}, max_chars={}",
                  config.context_prune.enabled, config.context_prune.protect_recent,
                  config.context_prune.soft_prune_lines, config.context_prune.hard_prune_after,
                  config.context_prune.max_tool_result_chars);

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

    // 压缩后重建全部缓存（历史已替换，增量状态失效）
    history_.invalidate_all_cache();

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

bool Session::force_compact(net::EventLoop& loop,
                            llm::ProviderClient& provider,
                            const llm::ToolRegistry& tools,
                            int max_compact_calls) {
    if (!compactor_) return false;

    // 构建 chat_fn（LLM 摘要生成）
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

    // 渐进式恢复参数（裁剪 + 压缩）
    struct RecoveryLevel {
        int hard_prune_after;       // 0=全量裁剪
        int max_tool_result_chars;  // 工具结果截断长度
        int soft_prune_lines;       // 软裁剪保留行数
        int keep_recent;            // 压缩保留近期轮次（0=使用默认值）
        const char* name;
    };
    const RecoveryLevel levels[] = {
        {10, 2000, 5, 0,  "L0"},  // 默认裁剪 + 强制压缩
        {5,  1000, 4, 0,  "L1"},  // 加码裁剪 + keep_recent 减半
        {3,   600, 3, 0,  "L2"},  // 激进裁剪
        {0,   400, 3, 3,  "L3"},  // 全量裁剪 + keep_recent=3
        {0,   200, 2, 1,  "L4"},  // 最激进裁剪 + keep_recent=1
    };

    auto context_limit = compactor_->config().context_length;
    auto safe_threshold = static_cast<int64_t>(context_limit * 0.7);  // 安全线：70%

    int compact_call_count = 0;

    for (int i = 0; i < 5; ++i) {
        const auto& lvl = levels[i];
        log::info_fmt("force_compact: {} before={} msgs",
                      lvl.name, history_.size());

        // 第一步：调整裁剪参数（纯本地，零开销）
        auto prune_cfg = history_.prune_config();
        prune_cfg.hard_prune_after = lvl.hard_prune_after;
        prune_cfg.max_tool_result_chars = lvl.max_tool_result_chars;
        prune_cfg.soft_prune_lines = lvl.soft_prune_lines;
        history_.set_prune_config(prune_cfg);
        history_.invalidate_all_cache();

        // 裁剪后先估算，裁剪够用就不压缩（省一次 LLM 调用）
        auto estimated = history_.pruned_tokens();
        log::info_fmt("force_compact: {} after prune, estimated_tokens={}, safe_threshold={}",
                      lvl.name, estimated, safe_threshold);

        if (estimated < safe_threshold) {
            log::info_fmt("force_compact: {} prune only sufficient", lvl.name);
            return true;
        }

        // 第二步：压缩（需调 LLM 生成摘要，成本高）
        // 检查 LLM 压缩调用次数限制
        if (compact_call_count >= max_compact_calls) {
            log::warn_fmt("force_compact: max_compact_calls={} reached, skipping compact",
                          max_compact_calls);
            continue;  // 仍然尝试下一级的裁剪参数
        }

        int keep = lvl.keep_recent;
        // L1: keep_recent 减半
        if (keep == 0 && i == 1) {
            keep = std::max(compactor_->config().keep_recent / 2, 3);
        }

        compactor_->compact(history_, chat_fn, keep);
        compact_call_count++;
        history_.invalidate_all_cache();

        // 压缩后再估算
        estimated = history_.pruned_tokens();
        log::info_fmt("force_compact: {} after compact ({}/{}), msgs={}, estimated_tokens={}",
                      lvl.name, compact_call_count, max_compact_calls,
                      history_.size(), estimated);

        if (estimated < safe_threshold) {
            log::info_fmt("force_compact: {} success", lvl.name);
            return true;
        }

        // 仍超限，继续下一级（更激进的裁剪+压缩）
        log::info_fmt("force_compact: {} still over limit, escalating", lvl.name);
    }

    // L4 后仍超限（理论上 keep_recent=1 不应超限，除非 system prompt 本身超长）
    log::error_fmt("force_compact: all levels exhausted, system prompt may be too long");
    return false;
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
        // 只恢复 user 和 assistant 消息，跳过工具调用中间步骤
        if (role == "tool_call" || role == "tool") continue;

        if (role == "user") {
            history_.add_user(content);
            continue;
        }

        if (role == "assistant") {
            history_.add_assistant(content);
            continue;
        }

    }

    log::info_fmt("session restored: id={}, messages={}",
                  std::string(session_id_.data(), session_id_.size()),
                  messages.size());
}

}  // namespace ben_gear::workspace
