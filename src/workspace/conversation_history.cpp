#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/adapter.hpp"
#include "ben_gear/workspace/uuid.hpp"

namespace ben_gear::workspace {

// ==================== 格式转换实现 ====================

Json ConversationHistory::to_openai_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 增量缓存：只转换新增的消息
    if (openai_cached_count_ == messages_.size()) {
        return cached_openai_msgs_;
    }
    
    // 转换新增的消息
    for (std::size_t i = openai_cached_count_; i < messages_.size(); ++i) {
        cached_openai_msgs_.push_back(
            llm::OpenAIAdapter::to_openai_format(messages_[i])
        );
    }
    
    openai_cached_count_ = messages_.size();
    return cached_openai_msgs_;
}

Json ConversationHistory::to_anthropic_messages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 增量缓存：只转换新增的消息
    if (anthropic_cached_count_ == messages_.size()) {
        return cached_anthropic_msgs_;
    }
    
    // 转换新增的消息（跳过 system）
    for (std::size_t i = anthropic_cached_count_; i < messages_.size(); ++i) {
        if (messages_[i].role() != acp::Role::System) {
            cached_anthropic_msgs_.push_back(
                llm::AnthropicAdapter::to_anthropic_format(messages_[i])
            );
        }
    }
    
    anthropic_cached_count_ = messages_.size();
    return cached_anthropic_msgs_;
}

container::String ConversationHistory::get_system_prompt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
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
