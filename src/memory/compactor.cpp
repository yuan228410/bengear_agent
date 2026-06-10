#include "ben_gear/memory/compactor.hpp"

#include <fstream>

namespace ben_gear::memory {

bool Compactor::should_compact(int64_t prompt_tokens) const {
    std::lock_guard lock(mutex_);
    auto hard_threshold = static_cast<int64_t>(
        config_.context_length * config_.context_usage_threshold);
    if (prompt_tokens > hard_threshold) return true;

    auto soft_threshold = static_cast<int64_t>(
        hard_threshold * config_.early_compact_ratio);
    if (prompt_tokens > soft_threshold && last_round_count_ > 0) return true;

    return false;
}

bool Compactor::should_compact_local(
    const workspace::ConversationHistory& history) const {
    auto tokens = ContextBuilder::estimate_messages_tokens(history);
    return should_compact(tokens);
}

void Compactor::compact(
    workspace::ConversationHistory& history,
    std::function<std::string(const std::string&)> chat_fn,
    int keep_recent_override) {
    auto rounds = split_rounds(history);
    if (rounds.size() <= 1) return;

    auto keep = keep_recent_override > 0
        ? std::min(keep_recent_override, static_cast<int>(rounds.size()))
        : determine_keep_rounds(rounds);

    container::Vector<Round> old_rounds;
    container::Vector<Round> recent_rounds;
    for (int i = 0; i < static_cast<int>(rounds.size()); ++i) {
        if (i < static_cast<int>(rounds.size()) - keep) {
            old_rounds.push_back(rounds[i]);
        } else {
            recent_rounds.push_back(rounds[i]);
        }
    }

    if (old_rounds.empty()) return;

    auto summaries = batch_summarize(old_rounds, chat_fn);

    workspace::ConversationHistory new_history;

    // 保留 system 消息
    for (const auto& msg : history.messages()) {
        if (msg.role() == acp::Role::System) {
            new_history.add_message(msg);
            break;
        }
    }

    // 添加摘要消息
    for (size_t i = 0; i < old_rounds.size(); ++i) {
        int round_idx = static_cast<int>(i);
        auto it = summaries.find(round_idx);
        if (it != summaries.end()) {
            auto user_text = old_rounds[i].user_msg.get_all_text();
            auto user_content =
                std::string(user_text.data(), user_text.size());
            if (user_content.size() > 100) {
                user_content = user_content.substr(0, 100) + "...";
            }
            new_history.add_user(container::String(user_content.c_str()));
            new_history.add_assistant(it->second);
        }
    }

    // 近期轮次完整保留
    for (const auto& round : recent_rounds) {
        new_history.add_message(round.user_msg);
        for (const auto& msg : round.execution) {
            new_history.add_message(msg);
        }
    }

    // 更新缓存
    {
        std::lock_guard lock(mutex_);
        container::Map<int, container::String> new_cache;
        for (auto& [idx, summary] : cached_summaries_) {
            new_cache[idx + static_cast<int>(old_rounds.size())] =
                std::move(summary);
        }
        for (auto& [idx, summary] : summaries) {
            new_cache[idx + static_cast<int>(old_rounds.size())] =
                std::move(summary);
        }
        cached_summaries_ = std::move(new_cache);
        last_round_count_ = static_cast<int>(old_rounds.size());

        while (cached_summaries_.size() >
               static_cast<size_t>(config_.max_cached_summaries)) {
            cached_summaries_.erase(cached_summaries_.begin()->first);
        }

        save_cache();
    }

    log::info_fmt(
        "compaction done: old_rounds={}, kept={}, summaries={}",
        old_rounds.size(), recent_rounds.size(), summaries.size());

    history.swap(new_history);
}

Compactor::Round::Round(const acp::ACPMessage& user)
    : user_msg(user) {}

container::Vector<Compactor::Round> Compactor::split_rounds(
    const workspace::ConversationHistory& history) {
    container::Vector<Round> rounds;
    Round* current = nullptr;

    for (const auto& msg : history.messages()) {
        if (msg.role() == acp::Role::System) continue;

        if (msg.role() == acp::Role::User) {
            rounds.push_back(Round(msg));
            current = &rounds.back();
            continue;
        }

        if (current) {
            current->execution.push_back(msg);
        }
    }

    return rounds;
}

int Compactor::determine_keep_rounds(
    const container::Vector<Round>& rounds) const {
    auto keep_budget = static_cast<int64_t>(
        config_.context_length * config_.keep_budget_ratio);
    int64_t budget_used = 0;
    int keep = 0;

    for (int i = static_cast<int>(rounds.size()) - 1; i >= 0; --i) {
        auto user_text = rounds[i].user_msg.get_all_text();
        int64_t round_tokens =
            ContextBuilder::estimate_text_tokens(std::string_view(user_text.data(),
                                                   user_text.size()));
        for (const auto& msg : rounds[i].execution) {
            auto text = msg.get_all_text();
            round_tokens +=
                ContextBuilder::estimate_text_tokens(std::string_view(text.data(),
                                                       text.size()));
        }
        if (budget_used + round_tokens > keep_budget) break;
        budget_used += round_tokens;
        keep++;

        if (keep >= config_.keep_recent) break;
    }

    return std::max(keep, 1);
}

container::Map<int, container::String> Compactor::batch_summarize(
    const container::Vector<Round>& old_rounds,
    std::function<std::string(const std::string&)> chat_fn) {
    container::Map<int, container::String> summaries;

    struct Candidate {
        int round_idx;
        std::string text;
    };
    container::Vector<Candidate> candidates;

    for (int i = 0; i < static_cast<int>(old_rounds.size()); ++i) {
        std::string text;

        auto user_text = old_rounds[i].user_msg.get_all_text();
        text += "User: ";
        text += std::string(user_text.data(), user_text.size());
        text += "\n";

        for (const auto& msg : old_rounds[i].execution) {
            if (msg.role() == acp::Role::Assistant) {
                text += "Assistant: ";
                auto assistant_text = msg.get_all_text();
                text +=
                    std::string(assistant_text.data(), assistant_text.size());
                text += "\n";
            } else if (msg.role() == acp::Role::Tool) {
                text += "Tool: ";
                auto tool_text = msg.get_all_text();
                auto output = std::string(tool_text.data(), tool_text.size());
                if (output.size() > 200)
                    output = output.substr(0, 200) + "...";
                text += output;
                text += "\n";
            }
        }

        if (text.size() < 100) {
            summaries[i] = container::String(text.c_str());
            continue;
        }

        if (text.size() > 4000) text = text.substr(0, 4000) + "...";

        candidates.push_back({i, text});
    }

    if (candidates.empty()) return summaries;

    // 批量摘要
    std::string batch_text;
    container::Vector<int> batch_indices;

    auto flush_batch = [&]() {
        if (batch_text.empty()) return;

        std::string prompt =
            "请为每轮对话生成简洁摘要，格式：[摘要] "
            "用户意图(10字内) | 关键操作(15字内) | 结果(10字内)\n"
            "要求：保留关键实体名、文件名、数值等具体信息，丢弃寒暄和重复内容。\n\n";
        for (size_t j = 0; j < batch_indices.size(); ++j) {
            prompt +=
                "<round_" + std::to_string(j) + ">\n";
            auto& text = candidates[batch_indices[j]].text;
            prompt += text.substr(0, 4000);
            prompt += "\n</round_" + std::to_string(j) + ">\n\n";
        }

        try {
            auto response = chat_fn(prompt);

            for (size_t j = 0; j < batch_indices.size(); ++j) {
                std::string tag =
                    "<round_" + std::to_string(j) + ">";
                auto start = response.find(tag);
                if (start == std::string::npos) {
                    auto& cand = candidates[batch_indices[j]];
                    std::string fallback = cand.text.substr(0, 500);
                    summaries[batch_indices[j]] =
                        container::String(fallback.c_str());
                    continue;
                }
                start += tag.size();
                std::string end_tag =
                    "</round_" + std::to_string(j) + ">";
                auto end = response.find(end_tag, start);
                std::string summary;
                if (end != std::string::npos) {
                    summary = response.substr(start, end - start);
                } else {
                    summary = response.substr(start, 200);
                }
                while (!summary.empty() &&
                       (summary.front() == '\n' ||
                        summary.front() == ' '))
                    summary.erase(0, 1);
                while (!summary.empty() &&
                       (summary.back() == '\n' ||
                        summary.back() == ' '))
                    summary.pop_back();

                summaries[batch_indices[j]] =
                    container::String(summary.c_str());
            }
        } catch (const std::exception& e) {
            log::error_fmt("batch summarize failed: {}", e.what());
            for (int idx : batch_indices) {
                std::string fallback =
                    candidates[idx].text.substr(0, 500);
                summaries[idx] = container::String(fallback.c_str());
            }
        }

        batch_text.clear();
        batch_indices.clear();
    };

    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (batch_text.size() + candidates[i].text.size() > 12000) {
            flush_batch();
        }
        batch_text += candidates[i].text;
        batch_text += "\n\n";
        batch_indices.push_back(i);
    }
    flush_batch();

    return summaries;
}

void Compactor::load_cache() {
    if (cache_path_.empty()) return;

    std::ifstream file(cache_path_, std::ios::binary);
    if (!file) return;

    std::string content{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};
    std::string err;
    auto json = parse_json(content, err);
    if (!err.empty()) return;

    std::lock_guard lock(mutex_);
    if (json.contains("summaries") && json["summaries"].is_object()) {
        for (auto it = json["summaries"].begin();
             it != json["summaries"].end(); ++it) {
            try {
                auto idx =
                    std::stoi(std::string(it.key().data(), it.key().size()));
                cached_summaries_[idx] = it.value().get<container::String>();
            } catch (const std::exception&) {
            }
        }
    }
    if (json.contains("last_round_count") &&
        json["last_round_count"].is_number()) {
        last_round_count_ = json["last_round_count"].get<int>();
    }

    log::info_fmt("compactor cache loaded: entries={}, last_round_count={}",
                  cached_summaries_.size(), last_round_count_);
}

void Compactor::save_cache() const {
    if (cache_path_.empty()) return;

    Json json;
    Json summaries = Json::object();
    for (const auto& [idx, summary] : cached_summaries_) {
        summaries[std::to_string(idx)] =
            std::string(summary.data(), summary.size());
    }
    json["summaries"] = summaries;
    json["last_round_count"] = last_round_count_;

    std::ofstream file(cache_path_, std::ios::binary | std::ios::trunc);
    if (file) {
        file << json.dump(2);
    } else {
        log::error_fmt("compactor cache save failed: {}",
                       cache_path_.string());
    }
}


Compactor::Compactor(Config config,
                     const MemoryStore& memory_store,
                     const EpisodeStore& episode_store,
                     const ContextBuilder& context_builder,
                     const std::filesystem::path& cache_dir)
    : config_(config),
      memory_store_(memory_store),
      episode_store_(episode_store),
      context_builder_(context_builder),
      cache_path_(cache_dir.empty() ? std::filesystem::path()
                                     : cache_dir / "compactor_cache.json") {
    load_cache();
}

}  // namespace ben_gear::memory
