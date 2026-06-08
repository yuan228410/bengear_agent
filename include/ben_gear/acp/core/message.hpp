#pragma once

#include "content_block.hpp"
#include "ben_gear/base/utils/json.hpp"

namespace ben_gear::acp {

// 使用命名空间别名简化代码
namespace container = base::container;

// ==================== ACP 消息（标准协议）====================

/// ACP 消息（Agent Communication Protocol）
/// 
/// 标准协议定义，只包含协议规定的字段和方法：
/// - role: 消息角色
/// - content: 内容块列表
/// - 序列化/反序列化
/// 
/// 不包含：
/// - 会话管理（session_id、message_id、timestamp）
/// - 业务逻辑（缓存、压缩等）
class ACPMessage {
public:
    // ==================== 构造函数 ====================
    
    ACPMessage() = default;
    
    /// 从角色和内容块构造
    ACPMessage(Role role, container::Vector<ContentBlock> content)
        : role_(role), content_(std::move(content)) {}
    
    /// 简化构造：用户消息
    static ACPMessage user_message(container::String text) {
        ACPMessage msg;
        msg.role_ = Role::User;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：助手消息
    static ACPMessage assistant_message(container::String text) {
        ACPMessage msg;
        msg.role_ = Role::Assistant;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：系统消息
    static ACPMessage system_message(container::String text) {
        ACPMessage msg;
        msg.role_ = Role::System;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：工具结果消息
    static ACPMessage tool_result_message(llm::ToolCallResult result) {
        ACPMessage msg;
        msg.role_ = Role::Tool;
        msg.add_tool_result(std::move(result));
        return msg;
    }
    
    // ==================== 属性访问 ====================
    
    /// 获取角色
    Role role() const noexcept { return role_; }
    
    /// 设置角色
    void set_role(Role role) { role_ = role; }
    
    /// 获取内容块列表
    const container::Vector<ContentBlock>& content() const noexcept { return content_; }
    
    /// 获取内容块列表（可修改）
    container::Vector<ContentBlock>& content() { return content_; }
    
    // ==================== 内容操作 ====================
    
    /// 添加内容块
    void add_content(ContentBlock block) {
        content_.push_back(std::move(block));
    }
    
    /// 添加文本内容
    void add_text(container::String text) {
        content_.push_back(ContentBlock::text(std::move(text)));
    }
    
    /// 添加工具调用
    void add_tool_use(llm::ToolCallRequest call) {
        content_.push_back(ContentBlock::tool_use(std::move(call)));
    }
    
    /// 添加工具结果
    void add_tool_result(llm::ToolCallResult result) {
        content_.push_back(ContentBlock::tool_result(std::move(result)));
    }
    
    /// 添加思考内容
    void add_thinking(container::String thinking) {
        content_.push_back(ContentBlock::thinking(std::move(thinking)));
    }
    
    // ==================== 序列化（标准协议）====================
    
    /// 序列化为 JSON
    Json to_json() const;
    
    /// 从 JSON 解析
    static ACPMessage from_json(const Json& j);
    
    // ==================== 工具方法 ====================
    
    /// 获取所有文本内容（拼接）
    container::String get_all_text() const {
        container::String result;
        for (const auto& block : content_) {
            if (block.is_text()) {
                if (!result.empty()) {
                    result.append("\n", 1);
                }
                result.append(block.text());
            }
        }
        return result;
    }
    
    /// 获取所有工具调用
    container::Vector<llm::ToolCallRequest> get_tool_calls() const {
        container::Vector<llm::ToolCallRequest> calls;
        for (const auto& block : content_) {
            if (block.is_tool_use()) {
                calls.push_back(block.tool_use());
            }
        }
        return calls;
    }
    
    /// 是否包含工具调用
    bool has_tool_calls() const {
        for (const auto& block : content_) {
            if (block.is_tool_use()) {
                return true;
            }
        }
        return false;
    }
    
    /// 获取所有工具结果
    container::Vector<llm::ToolCallResult> get_tool_results() const {
        container::Vector<llm::ToolCallResult> results;
        for (const auto& block : content_) {
            if (block.is_tool_result()) {
                results.push_back(block.tool_result());
            }
        }
        return results;
    }
    
private:
    Role role_ = Role::User;
    container::Vector<ContentBlock> content_;
};

} // namespace ben_gear::acp
