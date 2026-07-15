#pragma once

#include "capabilities/tool/acp/core/message.hpp"
#include <vector>
#include "base/utils/json.hpp"
#include "base/config/settings.hpp"

#include <chrono>
#include <mutex>

namespace ben_gear::llm {

namespace acp = ben_gear::acp;

// ==================== 会话历史管理 ====================

/// LLM 会话历史 — 纯消息容器 + 格式转换
///
/// 职责：
/// - 管理对话历史（基于 ACP 消息）
/// - OpenAI / Anthropic 格式转换（增量缓存）
/// - 会话元数据（session_id）
/// - 线程安全
///
/// 不包含上下文裁剪（裁剪逻辑在 memory/prune_utils.hpp 中作为自由函数）
class ConversationHistory {
public:
    ConversationHistory() = default;

    explicit ConversationHistory(std::string session_id)
        : session_id_(std::move(session_id)) {}

    // ─── 消息管理 ──────────────────────────────────────────────

    void add_message(const acp::ACPMessage& message);

    void add_system(const std::string& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::System);
        msg.add_text(content);
        add_message(msg);
    }

    void add_system(std::string_view content) {
        add_system(std::string(content.data(), content.size()));
    }

    bool set_system_prompt(std::string_view content) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string next(content.data(), content.size());
        bool found = false;
        bool changed = false;
        for (auto it = messages_.begin(); it != messages_.end();) {
            if (it->role() != acp::Role::System) { ++it; continue; }
            if (!found) {
                found = true;
                if (it->get_all_text() != next) {
                    *it = acp::ACPMessage::system_message(next);
                    changed = true;
                }
                ++it;
                continue;
            }
            it = messages_.erase(it);
            changed = true;
        }
        if (!found) {
            messages_.insert(messages_.begin(), acp::ACPMessage::system_message(next));
            changed = true;
        }
        if (changed) invalidate_all_cache();
        return changed;
    }

    void add_user(const std::string& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::User);
        msg.add_text(content);
        add_message(msg);
    }

    void add_user(std::string_view content) {
        add_user(std::string(content.data(), content.size()));
    }

    void add_assistant(const std::string& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::Assistant);
        msg.add_text(content);
        add_message(msg);
    }

    void add_assistant(std::string_view content) {
        add_assistant(std::string(content.data(), content.size()));
    }

    void add_tool_result(const std::string& tool_call_id,
                         [[maybe_unused]] const std::string& tool_name,
                         const std::string& result) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::Tool);
        ToolCallResult tool_result;
        tool_result.tool_call_id = tool_call_id;
        tool_result.output = result;
        msg.add_tool_result(tool_result);
        add_message(msg);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
        invalidate_all_cache();
    }

    // ─── 消息访问 ──────────────────────────────────────────────

    const std::vector<acp::ACPMessage>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }
    std::size_t size() const noexcept { return messages_.size(); }

    // ─── 格式转换（增量缓存）───────────────────────────────────

    Json to_openai_messages() const;
    Json to_anthropic_messages() const;
    std::string get_system_prompt() const;

    // ─── 会话元数据 ────────────────────────────────────────────

    const std::string& session_id() const noexcept { return session_id_; }
    void set_session_id(std::string id) { session_id_ = std::move(id); }
    std::string generate_message_id() const;

    // ─── 缓存管理 ──────────────────────────────────────────────

    void invalidate_cache() {
        cached_openai_msgs_ = Json::array();
        cached_anthropic_msgs_ = Json::array();
        openai_cached_count_ = 0;
        anthropic_cached_count_ = 0;
    }

    void invalidate_all_cache() {
        invalidate_cache();
        last_msg_count_ = 0;
    }

    void swap(ConversationHistory& other) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.swap(other.messages_);
        session_id_.swap(other.session_id_);
        cached_openai_msgs_ = other.cached_openai_msgs_;
        cached_anthropic_msgs_ = other.cached_anthropic_msgs_;
        std::swap(openai_cached_count_, other.openai_cached_count_);
        std::swap(anthropic_cached_count_, other.anthropic_cached_count_);
        invalidate_all_cache();
    }

    std::size_t openai_cached_count() const noexcept { return openai_cached_count_; }
    std::size_t anthropic_cached_count() const noexcept { return anthropic_cached_count_; }

    // ─── 公开：允许 memory 模块直接访问内部用于裁剪 ──────────────

    std::vector<acp::ACPMessage>& messages_mut() { return messages_; }
    std::mutex& mutex_ref() const { return mutex_; }
    std::size_t last_msg_count() const noexcept { return last_msg_count_; }
    void set_last_msg_count(std::size_t n) noexcept { last_msg_count_ = n; }

private:
    std::vector<acp::ACPMessage> messages_;
    std::string session_id_;

    mutable Json cached_openai_msgs_ = Json::array();
    mutable Json cached_anthropic_msgs_ = Json::array();
    mutable std::size_t openai_cached_count_ = 0;
    mutable std::size_t anthropic_cached_count_ = 0;
    mutable std::size_t last_msg_count_ = 0;

    mutable std::mutex mutex_;
};

}  // namespace ben_gear::llm
