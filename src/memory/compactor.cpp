#include "memory/compactor.hpp"

#include <fstream>
#include "base/log/logger.hpp"
#include "base/utils/string_utils.hpp"

namespace ben_gear::memory {

bool Compactor::should_compact(int64_t prompt_tokens) const {
    auto threshold = static_cast<int64_t>(
        config_.context_length * config_.context_usage_threshold);
    return prompt_tokens > threshold;
}

bool Compactor::should_compact_local(
    const workspace::ConversationHistory& history) const {
    auto tokens = history.pruned_tokens();
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

    std::vector<Round> old_rounds;
    std::vector<Round> recent_rounds;
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
                // 按 UTF-8 字符边界截断，避免劈开多字节字符产生非法 UTF-8
                user_content = std::string(utf8_truncate(user_content, 100)) + "...";
            }
            new_history.add_user(user_content);
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

    log::info_fmt(
        "compaction done: old_rounds={}, kept={}, summaries={}",
        old_rounds.size(), recent_rounds.size(), summaries.size());

    history.swap(new_history);
}

Compactor::Round::Round(const acp::ACPMessage& user)
    : user_msg(user) {}

std::vector<Compactor::Round> Compactor::split_rounds(
    const workspace::ConversationHistory& history) {
    std::vector<Round> rounds;
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
    const std::vector<Round>& rounds) const {
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

std::unordered_map<int, std::string> Compactor::batch_summarize(
    const std::vector<Round>& old_rounds,
    std::function<std::string(const std::string&)> chat_fn) {
    std::unordered_map<int, std::string> summaries;

    struct Candidate {
        int round_idx;
        std::string text;
    };
    std::vector<Candidate> candidates;

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
            summaries[i] = text;
            continue;
        }

        if (text.size() > 4000) text = text.substr(0, 4000) + "...";

        candidates.push_back({i, text});
    }

    if (candidates.empty()) return summaries;

    // 批量摘要
    std::string batch_text;
    std::vector<int> batch_indices;

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
                        fallback;
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
                    summary;
            }
        } catch (const std::exception& e) {
            log::error_fmt("batch summarize failed: {}", e.what());
            for (int idx : batch_indices) {
                std::string fallback =
                    candidates[idx].text.substr(0, 500);
                summaries[idx] = fallback;
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

Compactor::Compactor(Config config,
                     const MemoryStore& memory_store,
                     const EpisodeStore& episode_store,
                     const ContextBuilder& context_builder)
    : config_(config),
      memory_store_(memory_store),
      episode_store_(episode_store),
      context_builder_(context_builder) {}

}  // namespace ben_gear::memory