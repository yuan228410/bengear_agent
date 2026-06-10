#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/adapter.hpp"
#include "ben_gear/memory/context_pruner.hpp"
#include "ben_gear/workspace/uuid.hpp"

namespace ben_gear::workspace {

// ==================== 上下文裁剪 ====================

const container::Vector<acp::ACPMessage>& ConversationHistory::pruned_messages() const {
    if (!prune_dirty_) {
        return pruned_messages_;
    }

    // 需要重新计算裁剪
    if (!prune_config_.enabled) {
        log::debug_fmt("conversation_history: context_prune disabled, use raw messages");
        pruned_messages_ = messages_;
        prune_dirty_ = false;
        return pruned_messages_;
    }

    if (messages_.empty()) {
        pruned_messages_.clear();
        prune_dirty_ = false;
        return pruned_messages_;
    }

    // 裁剪启用且有消息：执行 prune
    memory::ContextPruner::Options opts;
    opts.protect_recent = prune_config_.protect_recent;
    opts.soft_prune_lines = prune_config_.soft_prune_lines;
    opts.hard_prune_after = prune_config_.hard_prune_after;
    opts.max_tool_result_chars = prune_config_.max_tool_result_chars;

    auto before_tokens = memory::ContextPruner::estimate_tokens(messages_);
    pruned_messages_ = memory::ContextPruner::prune(messages_, opts);
    auto after_tokens = memory::ContextPruner::estimate_tokens(pruned_messages_);

    if (before_tokens != after_tokens) {
        log::info_fmt("conversation_history: context_prune applied, {} msgs, tokens {} → {} (saved {})",
                      messages_.size(), before_tokens, after_tokens, before_tokens - after_tokens);
    } else {
        log::debug_fmt("conversation_history: context_prune no change, {} msgs, {} tokens",
                       messages_.size(), before_tokens);
    }

    prune_dirty_ = false;
    return pruned_messages_;
}

// ==================== 格式转换实现 ====================

Json ConversationHistory::to_openai_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& msgs = pruned_messages();

    // 增量缓存：只转换新增的消息
    if (openai_cached_count_ == msgs.size()) {
        return cached_openai_msgs_;
    }

    // 转换新增的消息
    for (std::size_t i = openai_cached_count_; i < msgs.size(); ++i) {
        cached_openai_msgs_.push_back(
            llm::OpenAIAdapter::to_openai_format(msgs[i])
        );
    }

    openai_cached_count_ = msgs.size();
    return cached_openai_msgs_;
}

Json ConversationHistory::to_anthropic_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& msgs = pruned_messages();

    // 增量缓存：只转换新增的消息
    if (anthropic_cached_count_ == msgs.size()) {
        return cached_anthropic_msgs_;
    }

    // 转换新增的消息（跳过 system）
    for (std::size_t i = anthropic_cached_count_; i < msgs.size(); ++i) {
        if (msgs[i].role() != acp::Role::System) {
            cached_anthropic_msgs_.push_back(
                llm::AnthropicAdapter::to_anthropic_format(msgs[i])
            );
        }
    }

    anthropic_cached_count_ = msgs.size();
    return cached_anthropic_msgs_;
}

container::String ConversationHistory::get_system_prompt() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 系统提示用原始消息（不裁剪）
    for (const auto& msg : messages_) {
        if (msg.role() == acp::Role::System) {
            return msg.get_all_text();
        }
    }
    return container::String();
}

container::String ConversationHistory::generate_message_id() const {
    return generate_uuid();
}

} // namespace ben_gear::workspace
