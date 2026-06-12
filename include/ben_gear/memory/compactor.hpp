#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/memory/context.hpp"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 上下文压缩器
/// 当对话 token 逼近上下文窗口时，压缩旧消息为摘要，保留近期消息完整
class Compactor {
public:
    struct Config {
        int64_t context_length = 256000;
        double context_usage_threshold = 0.8;
        double keep_budget_ratio = 0.2;
        double early_compact_ratio = 0.85;
        int keep_recent = 50;
        int max_cached_summaries = 200;
    };

    Compactor(Config config,
              const MemoryStore& memory_store,
              const EpisodeStore& episode_store,
              const ContextBuilder& context_builder,
              const std::filesystem::path& cache_dir = {});

    /// 判断是否需要压缩
    bool should_compact(int64_t prompt_tokens) const;

    /// 判断是否需要压缩（本地估算）
    bool should_compact_local(
        const workspace::ConversationHistory& history) const;

    /// 获取压缩配置
    const Config& config() const { return config_; }

    /// 执行压缩，直接修改传入的 history
    /// keep_recent_override: 覆盖 config_.keep_recent（用于 overflow 渐进恢复）
    void compact(
        workspace::ConversationHistory& history,
        std::function<std::string(const std::string&)> chat_fn,
        int keep_recent_override = 0);

private:
    /// 消息轮次
    struct Round {
        acp::ACPMessage user_msg;
        container::Vector<acp::ACPMessage> execution;

        explicit Round(const acp::ACPMessage& user);
    };

    container::Vector<Round> split_rounds(
        const workspace::ConversationHistory& history);
    int determine_keep_rounds(
        const container::Vector<Round>& rounds) const;
    container::Map<int, container::String> batch_summarize(
        const container::Vector<Round>& old_rounds,
        std::function<std::string(const std::string&)> chat_fn);

    void load_cache();
    void save_cache() const;

    Config config_;
    [[maybe_unused]] const MemoryStore& memory_store_;
    [[maybe_unused]] const EpisodeStore& episode_store_;
    [[maybe_unused]] const ContextBuilder& context_builder_;
    mutable std::mutex mutex_;
    container::Map<int, container::String> cached_summaries_;
    int last_round_count_ = 0;
    std::filesystem::path cache_path_;
};

}  // namespace ben_gear::memory
