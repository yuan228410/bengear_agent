#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/memory/context.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 上下文压缩器
/// 当对话 token 逼近上下文窗口时，压缩旧消息为摘要，保留近期消息完整
/// 操作 Session 独占的 history，不操作共享可变状态
class Compactor {
public:
    struct Config {
        int64_t context_length = 256000;       // 上下文窗口大小
        double context_usage_threshold = 0.8;  // 硬阈值比例
        double keep_budget_ratio = 0.2;        // 保留近期消息的预算比例
        double early_compact_ratio = 0.85;     // 软阈值比例（增量模式触发）
        int keep_recent = 50;                  // 最少保留的轮次数
        int max_cached_summaries = 200;        // 摘要缓存上限
    };

    Compactor(Config config,
              const MemoryStore& memory_store,
              const EpisodeStore& episode_store,
              const ContextBuilder& context_builder,
              const std::filesystem::path& cache_dir = {})
        : config_(config),
          memory_store_(memory_store),
          episode_store_(episode_store),
          context_builder_(context_builder),
          cache_path_(cache_dir.empty() ? std::filesystem::path() : cache_dir / "compactor_cache.json") {
        load_cache();
    }

    /// 判断是否需要压缩
    bool should_compact(int64_t prompt_tokens) const {
        std::lock_guard lock(mutex_);
        auto hard_threshold = static_cast<int64_t>(
            config_.context_length * config_.context_usage_threshold);
        if (prompt_tokens > hard_threshold) return true;

        auto soft_threshold = static_cast<int64_t>(
            hard_threshold * config_.early_compact_ratio);
        if (prompt_tokens > soft_threshold && last_round_count_ > 0) return true;

        return false;
    }

    /// 判断是否需要压缩（本地估算）
    bool should_compact_local(const llm::ConversationHistory& history) const {
        auto tokens = ContextBuilder::estimate_messages_tokens(history);
        return should_compact(tokens);
    }

    /// 执行压缩，返回压缩后的消息列表
    /// chat_fn: LLM 调用函数，用于生成摘要
    llm::ConversationHistory compact(
        llm::ConversationHistory history,
        std::function<std::string(const std::string&)> chat_fn) {
        auto rounds = split_rounds(history);
        if (rounds.size() <= 1) return history;

        auto keep = determine_keep_rounds(rounds);

        // 拆分旧轮次和近期轮次
        container::Vector<Round> old_rounds;
        container::Vector<Round> recent_rounds;
        for (int i = 0; i < static_cast<int>(rounds.size()); ++i) {
            if (i < static_cast<int>(rounds.size()) - keep) {
                old_rounds.push_back(rounds[i]);
            } else {
                recent_rounds.push_back(rounds[i]);
            }
        }

        if (old_rounds.empty()) return history;

        // 批量摘要旧轮次（纯计算，不操作缓存）
        auto summaries = batch_summarize(old_rounds, chat_fn);

        // 重组消息
        llm::ConversationHistory new_history;

        // 保留 system 消息
        for (const auto& msg : history.messages()) {
            if (msg.role == llm::MessageRole::system) {
                new_history.add_message(msg);
                break;
            }
        }

        // 添加摘要消息
        for (size_t i = 0; i < old_rounds.size(); ++i) {
            int round_idx = static_cast<int>(i);
            auto it = summaries.find(round_idx);
            if (it != summaries.end()) {
                auto user_content = std::string(old_rounds[i].user_msg.content.data(),
                                                old_rounds[i].user_msg.content.size());
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

        // 更新缓存（加锁保护）
        {
            std::lock_guard lock(mutex_);
            container::Map<int, container::String> new_cache;
            for (auto& [idx, summary] : cached_summaries_) {
                new_cache[idx + static_cast<int>(old_rounds.size())] = std::move(summary);
            }
            for (auto& [idx, summary] : summaries) {
                new_cache[idx + static_cast<int>(old_rounds.size())] = std::move(summary);
            }
            cached_summaries_ = std::move(new_cache);
            last_round_count_ = static_cast<int>(old_rounds.size());

            while (cached_summaries_.size() > static_cast<size_t>(config_.max_cached_summaries)) {
                cached_summaries_.erase(cached_summaries_.begin()->first);
            }

            save_cache();
        }

        log::info_fmt("compaction done: old_rounds={}, kept={}, summaries={}",
                      old_rounds.size(), recent_rounds.size(), summaries.size());

        return new_history;
    }

private:
    /// 消息轮次
    struct Round {
        llm::Message user_msg;
        container::Vector<llm::Message> execution;  // user 之后的 assistant/tool 消息
    };

    /// 将消息拆分为轮次
    container::Vector<Round> split_rounds(
        const llm::ConversationHistory& history) const {
        container::Vector<Round> rounds;
        Round* current = nullptr;

        for (const auto& msg : history.messages()) {
            if (msg.role == llm::MessageRole::system) continue;

            if (msg.role == llm::MessageRole::user) {
                // 新轮次：user 消息且不以 "[第" 开头
                auto content = std::string_view(msg.content.data(), msg.content.size());
                if (content.empty() || !content.starts_with("[第")) {
                    rounds.push_back(Round{msg, {}});
                    current = &rounds.back();
                    continue;
                }
            }

            // 添加到当前轮次的执行部分
            if (current) {
                current->execution.push_back(msg);
            }
        }

        return rounds;
    }

    /// 计算保留多少轮近期消息
    int determine_keep_rounds(const container::Vector<Round>& rounds) const {
        auto keep_budget = static_cast<int64_t>(
            config_.context_length * config_.context_usage_threshold * config_.keep_budget_ratio);
        if (keep_budget < 2000) keep_budget = 2000;

        int keep = 0;
        int64_t budget_used = 0;

        for (int i = static_cast<int>(rounds.size()) - 1; i >= 0; --i) {
            auto tokens = ContextBuilder::estimate_text_tokens(
                std::string_view(rounds[i].user_msg.content.data(),
                                 rounds[i].user_msg.content.size()));
            for (const auto& msg : rounds[i].execution) {
                tokens += ContextBuilder::estimate_text_tokens(
                    std::string_view(msg.content.data(), msg.content.size()));
            }
            if (budget_used + tokens > keep_budget) break;
            budget_used += tokens;
            keep++;
        }

        return std::max(keep, std::max(2, config_.keep_recent));
    }

    /// 批量摘要旧轮次
    container::Map<int, container::String> batch_summarize(
        const container::Vector<Round>& old_rounds,
        std::function<std::string(const std::string&)> chat_fn) {
        container::Map<int, container::String> summaries;

        // 收集需要 LLM 摘要的轮次
        struct Candidate {
            int index;
            std::string text;
        };
        container::Vector<Candidate> candidates;

        for (int i = 0; i < static_cast<int>(old_rounds.size()); ++i) {
            // 构建轮次文本
            std::string text;
            text += "User: ";
            text += std::string(old_rounds[i].user_msg.content.data(),
                               old_rounds[i].user_msg.content.size());
            text += "\n";
            for (const auto& msg : old_rounds[i].execution) {
                if (msg.role == llm::MessageRole::assistant) {
                    text += "Assistant: ";
                    text += std::string(msg.content.data(), msg.content.size());
                    text += "\n";
                } else if (msg.role == llm::MessageRole::tool) {
                    text += "Tool: ";
                    auto output = std::string(msg.content.data(), msg.content.size());
                    if (output.size() > 200) output = output.substr(0, 200) + "...";
                    text += output;
                    text += "\n";
                }
            }

            // 短文本直接用原文作为摘要
            if (text.size() < 100) {
                summaries[i] = container::String(text.c_str());
                continue;
            }

            // 截断到 4000 字符
            if (text.size() > 4000) text = text.substr(0, 4000) + "...";

            candidates.push_back({i, text});
        }

        if (candidates.empty()) return summaries;

        // 批量摘要（每批最多 12000 字符）
        std::string batch_text;
        container::Vector<int> batch_indices;

        auto flush_batch = [&]() {
            if (batch_text.empty()) return;

            std::string prompt = "Summarize each round in under 150 characters with: 需求 | 操作 | 结论\n\n";
            for (size_t j = 0; j < batch_indices.size(); ++j) {
                prompt += "<round_" + std::to_string(j) + ">\n";
                // 截断每轮到合理长度
                auto& text = candidates[batch_indices[j]].text;
                prompt += text.substr(0, 4000);
                prompt += "\n</round_" + std::to_string(j) + ">\n\n";
            }

            try {
                auto response = chat_fn(prompt);

                // 解析 <round_N> 标签
                for (size_t j = 0; j < batch_indices.size(); ++j) {
                    std::string tag = "<round_" + std::to_string(j) + ">";
                    auto start = response.find(tag);
                    if (start == std::string::npos) {
                        // 回退：截断原文
                        auto& cand = candidates[batch_indices[j]];
                        std::string fallback = cand.text.substr(0, 500);
                        summaries[batch_indices[j]] = container::String(fallback.c_str());
                        continue;
                    }
                    start += tag.size();
                    auto end_tag = "</round_" + std::to_string(j) + ">";
                    auto end = response.find(end_tag, start);
                    std::string summary;
                    if (end != std::string::npos) {
                        summary = response.substr(start, end - start);
                    } else {
                        summary = response.substr(start, 200);
                    }
                    // 去除首尾空白
                    while (!summary.empty() && (summary.front() == '\n' || summary.front() == ' '))
                        summary.erase(0, 1);
                    while (!summary.empty() && (summary.back() == '\n' || summary.back() == ' '))
                        summary.pop_back();

                    summaries[batch_indices[j]] = container::String(summary.c_str());
                }
            } catch (const std::exception& e) {
                log::error_fmt("batch summarize failed: {}", e.what());
                // 回退：截断原文
                for (int idx : batch_indices) {
                    std::string fallback = candidates[idx].text.substr(0, 500);
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

    Config config_;
    [[maybe_unused]] const MemoryStore& memory_store_;
    [[maybe_unused]] const EpisodeStore& episode_store_;
    [[maybe_unused]] const ContextBuilder& context_builder_;
    mutable std::mutex mutex_;
    container::Map<int, container::String> cached_summaries_;
    int last_round_count_ = 0;
    std::filesystem::path cache_path_;

    void load_cache() {
        if (cache_path_.empty()) return;

        std::ifstream file(cache_path_, std::ios::binary);
        if (!file) return;

        std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        std::string err;
        auto json = parse_json(content, err);
        if (!err.empty()) return;

        std::lock_guard lock(mutex_);
        if (json.contains("summaries") && json["summaries"].is_object()) {
            for (auto it = json["summaries"].begin(); it != json["summaries"].end(); ++it) {
                auto idx = std::stoi(it.key());
                cached_summaries_[idx] = container::String(it.value().get<std::string>().c_str());
            }
        }
        if (json.contains("last_round_count") && json["last_round_count"].is_number()) {
            last_round_count_ = json["last_round_count"].get<int>();
        }

        log::info_fmt("compactor cache loaded: entries={}, last_round_count={}",
                      cached_summaries_.size(), last_round_count_);
    }

    void save_cache() const {
        if (cache_path_.empty()) return;

        Json json;
        Json summaries = Json::object();
        for (const auto& [idx, summary] : cached_summaries_) {
            summaries[std::to_string(idx)] = std::string(summary.data(), summary.size());
        }
        json["summaries"] = summaries;
        json["last_round_count"] = last_round_count_;

        std::ofstream file(cache_path_, std::ios::binary | std::ios::trunc);
        if (file) {
            file << json.dump(2);
        } else {
            log::error_fmt("compactor cache save failed: {}", cache_path_.string());
        }
    }
};

}  // namespace ben_gear::memory
