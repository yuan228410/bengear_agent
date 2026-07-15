#pragma once

#include <vector>
#include <unordered_map>
#include "base/utils/json.hpp"
#include "llm/conversation_history.hpp"
#include "memory/store.hpp"
#include "memory/episode.hpp"
#include "memory/context.hpp"

#include <filesystem>
#include <functional>
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
        int keep_recent = 50;
    };

    Compactor(Config config,
              const MemoryStore& memory_store,
              const EpisodeStore& episode_store,
              const ContextBuilder& context_builder);

    /// 判断是否需要压缩
    bool should_compact(int64_t prompt_tokens) const;

    /// 判断是否需要压缩（本地估算）
    bool should_compact_local(
        const llm::ConversationHistory& history) const;

    /// 获取压缩配置
    const Config& config() const { return config_; }

    /// 执行压缩，直接修改传入的 history
    /// keep_recent_override: 覆盖 config_.keep_recent（用于 overflow 渐进恢复）
    void compact(
        llm::ConversationHistory& history,
        std::function<std::string(const std::string&)> chat_fn,
        int keep_recent_override = 0);

private:
    /// 消息轮次
    struct Round {
        acp::ACPMessage user_msg;
        std::vector<acp::ACPMessage> execution;

        explicit Round(const acp::ACPMessage& user);
    };

    std::vector<Round> split_rounds(
        const llm::ConversationHistory& history);
    int determine_keep_rounds(
        const std::vector<Round>& rounds) const;
    std::unordered_map<int, std::string> batch_summarize(
        const std::vector<Round>& old_rounds,
        std::function<std::string(const std::string&)> chat_fn);

    void load_cache();
    void save_cache() const;

    Config config_;
    const MemoryStore& memory_store_;
    const EpisodeStore& episode_store_;
    const ContextBuilder& context_builder_;
};

}  // namespace ben_gear::memory
