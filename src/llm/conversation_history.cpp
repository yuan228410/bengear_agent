#include "llm/conversation_history.hpp"
#include "llm/adapter.hpp"
#include "base/utils/uuid.hpp"

namespace ben_gear::llm {

// ==================== 消息管理 ====================

void ConversationHistory::add_message(const acp::ACPMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(message);
    invalidate_cache();
}

void ConversationHistory::add_message(acp::ACPMessage&& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(std::move(message));
    invalidate_cache();
}

// ==================== 格式转换（增量缓存） ====================

const Json& ConversationHistory::to_openai_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (openai_cached_count_ == messages_.size()) {
        return cached_openai_msgs_;
    }

    for (std::size_t i = openai_cached_count_; i < messages_.size(); ++i) {
        cached_openai_msgs_.push_back(
            OpenAIAdapter::to_openai_format(messages_[i]));
    }

    openai_cached_count_ = messages_.size();
    return cached_openai_msgs_;
}

const Json& ConversationHistory::to_anthropic_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (anthropic_cached_count_ == messages_.size()) {
        return cached_anthropic_msgs_;
    }

    for (std::size_t i = anthropic_cached_count_; i < messages_.size(); ++i) {
        if (messages_[i].role() != acp::Role::System) {
            cached_anthropic_msgs_.push_back(
                AnthropicAdapter::to_anthropic_format(messages_[i]));
        }
    }

    anthropic_cached_count_ = messages_.size();
    return cached_anthropic_msgs_;
}

std::string ConversationHistory::get_system_prompt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& msg : messages_) {
        if (msg.role() == acp::Role::System) {
            return msg.get_all_text();
        }
    }
    return std::string();
}

std::string ConversationHistory::generate_message_id() const {
    return base::utils::generate_uuid();
}

}  // namespace ben_gear::llm
