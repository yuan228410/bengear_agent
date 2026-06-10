#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/config/settings.hpp"

#include <chrono>
#include <mutex>

namespace ben_gear::workspace {

// 使用命名空间别名简化代码
namespace container = base::container;
namespace acp = ben_gear::acp;

// ==================== 会话历史管理 ====================

/// 会话历史管理（业务层）
///
/// 职责：
/// - 管理对话历史（基于 ACP 消息）
/// - 上下文裁剪（ContextPruner 三级策略，减少 prompt token）
/// - 增量缓存优化（OpenAI/Anthropic 格式，基于裁剪后消息）
/// - 会话元数据（session_id、message_id、timestamp）
/// - 线程安全
///
/// 不包含：
/// - 协议定义（由 acp 模块负责）
/// - 格式转换（由 llm adapter 负责）
class ConversationHistory {
public:
    // ==================== 构造函数 ====================

    ConversationHistory() = default;

    /// 构造并设置 session_id
    explicit ConversationHistory(container::String session_id)
        : session_id_(std::move(session_id)) {}

    // ==================== 消息管理 ====================

    /// 添加消息
    void add_message(const acp::ACPMessage& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push_back(message);
        invalidate_cache();
    }

    /// 添加系统消息
    void add_system(const container::String& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::System);
        msg.add_text(content);
        add_message(msg);
    }

    /// 添加系统消息（string_view 重载）
    void add_system(std::string_view content) {
        add_system(container::String(content.data(), content.size()));
    }

    /// 添加用户消息
    void add_user(const container::String& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::User);
        msg.add_text(content);
        add_message(msg);
    }

    /// 添加用户消息（string_view 重载）
    void add_user(std::string_view content) {
        add_user(container::String(content.data(), content.size()));
    }

    /// 添加助手消息
    void add_assistant(const container::String& content) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::Assistant);
        msg.add_text(content);
        add_message(msg);
    }

    /// 添加助手消息（string_view 重载）
    void add_assistant(std::string_view content) {
        add_assistant(container::String(content.data(), content.size()));
    }

    /// 添加工具结果
    void add_tool_result(const container::String& tool_call_id,
                         [[maybe_unused]] const container::String& tool_name,
                         const container::String& result) {
        acp::ACPMessage msg;
        msg.set_role(acp::Role::Tool);

        llm::ToolCallResult tool_result;
        tool_result.tool_call_id = tool_call_id;
        tool_result.output = result;
        msg.add_tool_result(tool_result);

        add_message(msg);
    }

    /// 清空历史
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
        invalidate_cache();
    }

    // ==================== 消息访问 ====================

    /// 获取原始消息列表（完整数据，用于持久化等）
    const container::Vector<acp::ACPMessage>& messages() const noexcept {
        return messages_;
    }

    /// 是否为空
    bool empty() const noexcept {
        return messages_.empty();
    }

    /// 消息数量
    std::size_t size() const noexcept {
        return messages_.size();
    }

    // ==================== 上下文裁剪 ====================

    /// 设置裁剪配置（必须在序列化前调用）
    void set_prune_config(const config::ContextPruneSettings& cfg) {
        std::lock_guard<std::mutex> lock(mutex_);
        prune_config_ = cfg;
        prune_dirty_ = true;
        invalidate_cache();
    }

    /// 获取当前裁剪配置
    config::ContextPruneSettings prune_config() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return prune_config_;
    }

    /// 获取裁剪后的消息列表（懒计算，线程安全）
    const container::Vector<acp::ACPMessage>& pruned_messages() const;

    // ==================== 格式转换（增量缓存，基于裁剪后消息）====================

    /// 转换为 OpenAI 格式（增量缓存，基于裁剪后消息）
    Json to_openai_messages() const;

    /// 转换为 Anthropic 格式（增量缓存，基于裁剪后消息，不含 system）
    Json to_anthropic_messages() const;

    /// 获取系统提示（Anthropic 用）
    container::String get_system_prompt() const;

    // ==================== 会话元数据 ====================

    /// 获取 session_id
    const container::String& session_id() const noexcept { return session_id_; }

    /// 设置 session_id
    void set_session_id(container::String id) { session_id_ = std::move(id); }

    /// 生成唯一 message_id
    container::String generate_message_id() const;

    // ==================== 缓存管理 ====================

    /// 使缓存失效（compaction 等替换整个 history 后需调用）
    void invalidate_cache() {
        cached_openai_msgs_ = Json::array();
        cached_anthropic_msgs_ = Json::array();
        openai_cached_count_ = 0;
        anthropic_cached_count_ = 0;
        prune_dirty_ = true;
    }

    /// 交换内容（用于 compaction）
    void swap(ConversationHistory& other) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.swap(other.messages_);
        session_id_.swap(other.session_id_);
        cached_openai_msgs_ = other.cached_openai_msgs_;
        cached_anthropic_msgs_ = other.cached_anthropic_msgs_;
        std::swap(openai_cached_count_, other.openai_cached_count_);
        std::swap(anthropic_cached_count_, other.anthropic_cached_count_);
        prune_dirty_ = true;
    }

    /// 获取 OpenAI 缓存数量（用于优化）
    std::size_t openai_cached_count() const noexcept {
        return openai_cached_count_;
    }

    /// 获取 Anthropic 缓存数量（用于优化）
    std::size_t anthropic_cached_count() const noexcept {
        return anthropic_cached_count_;
    }

private:
    // 消息列表（原始完整数据）
    container::Vector<acp::ACPMessage> messages_;

    // 会话元数据
    container::String session_id_;

    // 上下文裁剪
    config::ContextPruneSettings prune_config_;
    mutable container::Vector<acp::ACPMessage> pruned_messages_;
    mutable bool prune_dirty_ = true;

    // 增量缓存（mutable 允许 const 方法修改）
    mutable Json cached_openai_msgs_ = Json::array();
    mutable Json cached_anthropic_msgs_ = Json::array();
    mutable std::size_t openai_cached_count_ = 0;
    mutable std::size_t anthropic_cached_count_ = 0;

    // 线程安全
    mutable std::mutex mutex_;
};

} // namespace ben_gear::workspace
