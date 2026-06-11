#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/adapter.hpp"
#include "ben_gear/workspace/uuid.hpp"

namespace ben_gear::workspace {

// ==================== 上下文裁剪 ====================

const container::Vector<acp::ACPMessage>& ConversationHistory::pruned_messages() const {
    if (!prune_dirty_) {
        return pruned_messages_;
    }

    // 裁剪未启用：直接用原始消息
    if (!prune_config_.enabled) {
        log::debug_fmt("conversation_history: context_prune disabled, use raw messages");
        pruned_messages_ = messages_;
        prune_dirty_ = false;
        last_pruned_msg_count_ = messages_.size();
        cached_pruned_tokens_ = cached_original_tokens_;  // 未裁剪，token 相同
        return pruned_messages_;
    }

    if (messages_.empty()) {
        pruned_messages_.clear();
        prune_dirty_ = false;
        last_pruned_msg_count_ = 0;
        cached_pruned_tokens_ = 0;
        return pruned_messages_;
    }

    memory::ContextPruner::Options opts;
    opts.protect_recent = prune_config_.protect_recent;
    opts.soft_prune_lines = prune_config_.soft_prune_lines;
    opts.hard_prune_after = prune_config_.hard_prune_after;
    opts.max_tool_result_chars = prune_config_.max_tool_result_chars;

    const std::size_t total = messages_.size();
    const std::size_t prev_count = last_pruned_msg_count_;

    // ============================================================
    // 增量裁剪路径
    // 核心不变量：once hard-pruned, always hard-pruned (depth 只增不减)
    // 冻结区 = old_depth > hard_prune_after 的消息，输出恒为 [tool result pruned]
    // 活跃区 = 可能跨 zone 边界的消息 + 新增消息
    // ============================================================
    if (prev_count > 0 && prev_count <= total && !pruned_messages_.empty()) {
        // 计算新增的 assistant 消息数
        int new_asst = 0;
        for (std::size_t i = prev_count; i < total; ++i) {
            if (messages_[i].role() == acp::Role::Assistant) {
                new_asst++;
            }
        }

        // 冻结判定：old_depth > hard_prune_after ⟺ current_depth > hard_prune_after + new_asst
        const int freeze_depth_threshold = opts.hard_prune_after + new_asst;

        // 计算全量 depth（O(n) 整数计数，极轻量）
        auto depths = memory::ContextPruner::compute_depths(messages_);

        // 找冻结区边界：从前往后扫描，找到第一个 assistant with depth <= freeze_depth_threshold
        std::size_t freeze_end = 0;
        for (std::size_t i = 0; i < prev_count; ++i) {
            if (depths[i] > 0 && depths[i] <= freeze_depth_threshold) {
                break;  // 活跃区开始
            }
            freeze_end = i + 1;
        }

        if (freeze_end > 0) {
            // 冻结区：从 pruned_messages_ 缓存复用（输出不变）
            container::Vector<acp::ACPMessage> result;
            result.reserve(total);
            for (std::size_t i = 0; i < freeze_end; ++i) {
                result.push_back(pruned_messages_[i]);
            }

            // 活跃区：重算 [freeze_end, total)
            auto active_result = memory::ContextPruner::prune_range_with_depths(
                messages_, freeze_end, depths, opts);

            for (auto& msg : active_result.messages) {
                result.push_back(std::move(msg));
            }

            const std::size_t active_count = total - freeze_end;
            pruned_messages_ = std::move(result);
            prune_dirty_ = false;
            last_pruned_msg_count_ = total;
            cached_pruned_tokens_ = -1;  // 懒计算

            log::info_fmt("conversation_history: context_prune incremental, "
                          "total={} msgs, freeze={}, active={}, hard={}, soft={}",
                          total, freeze_end, active_count,
                          active_result.hard_pruned, active_result.soft_pruned);
            return pruned_messages_;
        }
        // freeze_end == 0: 没有冻结区（所有旧消息都在活跃区），退化为全量
    }

    // ============================================================
    // 全量裁剪
    // ============================================================
    auto pruned_result = memory::ContextPruner::prune(messages_, opts);
    pruned_messages_ = std::move(pruned_result.messages);

    prune_dirty_ = false;
    last_pruned_msg_count_ = total;
    cached_pruned_tokens_ = -1;  // 懒计算

    auto orig_tok = original_tokens();
    auto prun_tok = pruned_tokens();
    if (orig_tok != prun_tok) {
        log::info_fmt("conversation_history: context_prune applied, {} msgs, tokens {} → {} (saved {})",
                      total, orig_tok, prun_tok, orig_tok - prun_tok);
    } else {
        log::debug_fmt("conversation_history: context_prune no change, {} msgs, {} tokens",
                       total, orig_tok);
    }

    return pruned_messages_;
}

// ==================== Token 缓存 ====================

int64_t ConversationHistory::pruned_tokens() const {
    if (cached_pruned_tokens_ < 0) {
        const auto& msgs = pruned_messages();  // 触发裁剪（增量或全量）
        cached_pruned_tokens_ = memory::ContextPruner::estimate_tokens(msgs);
    }
    return cached_pruned_tokens_;
}

int64_t ConversationHistory::original_tokens() const {
    if (cached_original_tokens_ < 0) {
        cached_original_tokens_ = memory::ContextPruner::estimate_tokens(messages_);
    }
    return cached_original_tokens_;
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
