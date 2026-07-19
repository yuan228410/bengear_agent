#include "workspace/session.hpp"
#include "base/utils/uuid.hpp"
#include "llm/provider_client.hpp"
#include "workspace/history_db.hpp"
#include "base/log/logger.hpp"
#include "memory/compactor.hpp"
#include "memory/updater.hpp"
#include "memory/episode.hpp"
#include "memory/prune_utils.hpp"
#include "memory/memory_tools.hpp"

namespace ben_gear::workspace {

Session::~Session() = default;

Session::Session(SessionConfig config, SessionDeps deps,
                 capabilities::tool::ToolRegistry& tools)
    : session_id_(config.session_id.empty()
                      ? ::ben_gear::base::utils::generate_uuid()
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
    memory::register_episode_tools(tools, episode_store_);

    // 设置上下文裁剪配置（存储在 Session 中，通过 PruneUtils 应用）
    prune_config_ = config.context_prune;
    memory::PruneUtils::apply_prune(history_, prune_config_);
    log::info_fmt("session context_prune: enabled={}, protect_recent={}, soft_lines={}, hard_after={}, max_chars={}",
                  config.context_prune.enabled, config.context_prune.protect_recent,
                  config.context_prune.soft_prune_lines, config.context_prune.hard_prune_after,
                  config.context_prune.max_tool_result_chars);

    // 创建会话级 Compactor 和 MemoryUpdater
    memory::Compactor::Config compactor_cfg;
    compactor_cfg.context_length = config.context_length;
    compactor_ = std::make_unique<memory::Compactor>(
        compactor_cfg, *memory_store_, *episode_store_,
        *deps.context_builder);
    memory_updater_ = std::make_unique<memory::MemoryUpdater>(
        *memory_store_, *episode_store_,
        ws_ctx_.tier_paths.workspace_dir / "sessions");

    log::info_fmt("session created: id={}",
                  std::string(session_id_.data(), session_id_.size()));
}

void Session::maybe_compact(net::EventLoop& loop,
                            llm::ProviderClient& provider,
                            const capabilities::tool::ToolRegistry& tools) {
    if (!compactor_ || !compactor_->should_compact_local(history_)) return;

    auto chat_fn = [&loop, &provider,
                    &tools](const std::string& prompt) -> std::string {
        llm::ConversationHistory tmp;
        tmp.add_user(std::string(prompt.data(), prompt.size()));
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

    // 压缩前从原始历史收集 round summaries，避免压缩后取到摘要而非原始内容
    std::vector<std::string> summaries;
    {
        auto& msgs = history_.messages();
        for (size_t i = 0; i < msgs.size(); ++i) {
            if (msgs[i].role() != acp::Role::User) continue;
            auto user_text = msgs[i].get_all_text();
            auto user_content = std::string(user_text.data(), user_text.size());
            if (user_content.size() > 100)
                user_content = user_content.substr(0, 100) + "...";
            std::string assistant_content;
            for (size_t j = i + 1; j < msgs.size(); ++j) {
                if (msgs[j].role() == acp::Role::Assistant) {
                    auto at = msgs[j].get_all_text();
                    assistant_content = std::string(at.data(), at.size());
                    if (assistant_content.size() > 200)
                        assistant_content = assistant_content.substr(0, 200) + "...";
                    break;
                }
            }
            if (!assistant_content.empty()) {
                auto s = "用户: " + user_content + "\n助手: " + assistant_content;
                summaries.push_back(std::string(s.data(), s.size()));
            }
        }
    }

    compactor_->compact(history_, chat_fn);
    history_.invalidate_all_cache();

    if (memory_updater_ && !summaries.empty()) {
        memory_updater_->update(summaries, chat_fn);
    }
    log::info_fmt("session compacted: history_size={}", history_.size());
}

bool Session::force_compact(net::EventLoop& loop,
                            llm::ProviderClient& provider,
                            const capabilities::tool::ToolRegistry& tools,
                            int max_compact_calls) {
    if (!compactor_) return false;

    // 构建 chat_fn（LLM 摘要生成）
    auto chat_fn = [&loop, &provider,
                    &tools](const std::string& prompt) -> std::string {
        llm::ConversationHistory tmp;
        tmp.add_user(std::string(prompt.data(), prompt.size()));
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
        {10, 2000, 5, 0,  "L0"},  // 默认裁剪 + 默认预算
        {5,  1000, 4, 0,  "L1"},  // 加码裁剪 + keep_recent 减半（代码特例）
        {3,   600, 3, 0,  "L2"},  // 激进裁剪 + keep_recent 再减半
        {0,   400, 3, 3,  "L3"},  // 全量裁剪 + keep_recent=3
        {0,   200, 2, 1,  "L4"},  // 最激进裁剪 + keep_recent=1
    };

    auto context_limit = compactor_->config().context_length;
    auto safe_threshold = static_cast<int64_t>(context_limit * 0.7);  // 安全线：70%

    int compact_call_count = 0;
    std::vector<std::string> all_summaries;  // 累积各轮摘要

    for (int i = 0; i < 5; ++i) {
        const auto& lvl = levels[i];
        log::info_fmt("force_compact: {} before={} msgs",
                      lvl.name, history_.size());

        // 第一步：调整裁剪参数（纯本地，零开销）
        prune_config_.hard_prune_after = lvl.hard_prune_after;
        prune_config_.max_tool_result_chars = lvl.max_tool_result_chars;
        prune_config_.soft_prune_lines = lvl.soft_prune_lines;
        memory::PruneUtils::apply_prune(history_, prune_config_);

        // 裁剪后先估算，裁剪够用就不压缩（省一次 LLM 调用）
        auto estimated = memory::PruneUtils::estimate_tokens(history_);
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
        if (keep == 0) {
            keep = std::max(compactor_->config().keep_recent / (1 << i), 3);
        }

        // 压缩前收集原始摘要（压缩后助手被替换为摘要，内容失真）
        {
            auto& msgs = history_.messages();
            for (size_t j = 0; j < msgs.size(); ++j) {
                if (msgs[j].role() != acp::Role::User) continue;
                auto ut = msgs[j].get_all_text();
                auto uc = std::string(ut.data(), ut.size());
                if (uc.size() > 100) uc = uc.substr(0, 100) + "...";
                std::string ac;
                for (size_t k = j + 1; k < msgs.size(); ++k) {
                    if (msgs[k].role() == acp::Role::Assistant) {
                        auto at = msgs[k].get_all_text();
                        ac = std::string(at.data(), at.size());
                        if (ac.size() > 200) ac = ac.substr(0, 200) + "...";
                        break;
                    }
                }
                if (!ac.empty()) {
                    auto s = "用户: " + uc + "\n助手: " + ac;
                    all_summaries.push_back(std::string(s.data(), s.size()));
                }
            }
        }

        compactor_->compact(history_, chat_fn, keep);
        compact_call_count++;
        history_.invalidate_all_cache();

        // 压缩后再估算
        estimated = memory::PruneUtils::estimate_tokens(history_);
        log::info_fmt("force_compact: {} after compact ({}/{}), msgs={}, estimated_tokens={}",
                      lvl.name, compact_call_count, max_compact_calls,
                      history_.size(), estimated);

        if (estimated < safe_threshold) {
            log::info_fmt("force_compact: {} success", lvl.name);
            if (memory_updater_ && !all_summaries.empty()) {
                memory_updater_->update(all_summaries, chat_fn);
            }
            return true;
        }

        // 仍超限，继续下一级（更激进的裁剪+压缩）
        log::info_fmt("force_compact: {} still over limit, escalating", lvl.name);
    }

    // L4 后仍超限（理论上 keep_recent=1 不应超限，除非 system prompt 本身超长）
    log::error_fmt("force_compact: all levels exhausted, system prompt may be too long");
    return false;
}

void Session::persist_message(const std::string& role,
                              const std::string& content,
                              workspace::HistoryDB& db) {
    db.append(session_id_, role, content);
}

void Session::persist_assistant_message(
    const std::string& content,
    const std::vector<acp::ToolCallRequest>& tool_calls,
    workspace::HistoryDB& db) {
    db.append(session_id_,
              std::string("assistant"), content);
    for (const auto& call : tool_calls) {
        auto args_str = call.arguments.dump();
        db.append(session_id_,
                  std::string("tool_call"),
                  std::string(args_str.data(), args_str.size()), call.id, call.name);
    }
}

void Session::persist_assistant_with_tools(
    const std::string& content,
    const std::vector<acp::ToolCallRequest>& tool_calls,
    workspace::HistoryDB& db) {
    persist_assistant_message(content, tool_calls, db);
}

void Session::persist_tool_result(const std::string& tool_call_id,
                                  const std::string& tool_name,
                                  const std::string& content,
                                  workspace::HistoryDB& db) {
    db.append(session_id_,
              std::string("tool"), content, tool_call_id, tool_name);
}

void Session::restore_from_db(workspace::HistoryDB& db) {
    db.flush();
    auto messages =
        db.load_session(session_id_);

    for (size_t i = 0; i < messages.size(); ++i) {
        auto role = messages[i].value("role", "");
        auto content_val = messages[i].value("content", "");
        auto content = std::string(content_val.data(), content_val.size());

        if (role == "system" || role == "thinking" || role == "plan_anchor") continue;

        if (role == "user") {
            history_.add_user(content);
            continue;
        }

        if (role == "assistant") {
            acp::ACPMessage msg;
            msg.set_role(acp::Role::Assistant);
            if (!content.empty()) msg.add_text(content);

            while (i + 1 < messages.size() && messages[i + 1].value("role", "") == "tool_call") {
                ++i;
                auto args_text = messages[i].value("content", "");
                std::string error;
                auto args = parse_json(std::string_view(args_text.data(), args_text.size()), error);
                if (!error.empty()) {
                    args = Json{{"_raw_arguments", args_text}, {"_parse_error", error}};
                }
                acp::ToolCallRequest call;
                auto tid = messages[i].value("tool_call_id", "");
                auto tn = messages[i].value("tool_name", "");
                call.id = std::string(tid.data(), tid.size());
                call.name = std::string(tn.data(), tn.size());
                call.arguments = std::move(args);
                msg.add_tool_use(std::move(call));
            }

            if (!msg.content().empty()) history_.add_message(std::move(msg));
            continue;
        }

        if (role == "tool") {
            acp::ToolCallResult result;
            auto tid = messages[i].value("tool_call_id", "");
            auto tn = messages[i].value("tool_name", "");
            result.tool_call_id = std::string(tid.data(), tid.size());
            result.name = std::string(tn.data(), tn.size());
            result.output = content;
            result.success = true;
            history_.add_message(acp::ACPMessage::tool_result_message(std::move(result)));
            continue;
        }
    }

    log::info_fmt("session restored: id={}, messages={}",
                  std::string(session_id_.data(), session_id_.size()),
                  messages.size());
}

}  // namespace ben_gear::workspace
