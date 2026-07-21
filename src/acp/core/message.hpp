#pragma once

#include "content_block.hpp"
#include "base/utils/json.hpp"

namespace ben_gear::acp {

// 使用命名空间别名简化代码

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
    ACPMessage(Role role, std::vector<ContentBlock> content)
        : role_(role), content_(std::move(content)) {}
    
    /// 简化构造：用户消息
    static ACPMessage user_message(std::string text) {
        ACPMessage msg;
        msg.role_ = Role::User;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：助手消息
    static ACPMessage assistant_message(std::string text) {
        ACPMessage msg;
        msg.role_ = Role::Assistant;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：系统消息
    static ACPMessage system_message(std::string text) {
        ACPMessage msg;
        msg.role_ = Role::System;
        msg.content_.push_back(ContentBlock::text(std::move(text)));
        return msg;
    }
    
    /// 简化构造：工具结果消息
    static ACPMessage tool_result_message(ToolCallResult result) {
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
    const std::vector<ContentBlock>& content() const noexcept { return content_; }
    
    /// 获取内容块列表（可修改）
    std::vector<ContentBlock>& content() { return content_; }
    
    // ==================== 内容操作 ====================
    
    /// 添加内容块
    void add_content(ContentBlock block) {
        content_.push_back(std::move(block));
    }
    
    /// 添加文本内容
    void add_text(std::string text) {
        content_.push_back(ContentBlock::text(std::move(text)));
    }
    
    /// 添加工具调用
    void add_tool_use(ToolCallRequest call) {
        content_.push_back(ContentBlock::tool_use(std::move(call)));
    }
    
    /// 添加工具结果
    void add_tool_result(ToolCallResult result) {
        content_.push_back(ContentBlock::tool_result(std::move(result)));
    }
    
    /// 添加思考内容
    void add_thinking(std::string thinking) {
        content_.push_back(ContentBlock::thinking(std::move(thinking)));
    }
    
    // ==================== 序列化（标准协议）====================
    
    /// 序列化为 JSON
    Json to_json() const;
    
    /// 从 JSON 解析
    static ACPMessage from_json(const Json& j);
    
    // ==================== 工具方法 ====================
    
    /// 获取所有文本内容（拼接）
    std::string get_all_text() const {
        std::string result;
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
    std::vector<ToolCallRequest> get_tool_calls() const {
        std::vector<ToolCallRequest> calls;
        for_each_tool_call([&](const ToolCallRequest& call) {
            calls.push_back(call);
        });
        return calls;
    }

    /// 遍历工具调用引用，避免热点路径复制参数 JSON
    template <typename Fn>
    void for_each_tool_call(Fn&& fn) const {
        for (const auto& block : content_) {
            if (block.is_tool_use()) {
                fn(block.tool_use());
            }
        }
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
    std::vector<ToolCallResult> get_tool_results() const {
        std::vector<ToolCallResult> results;
        for_each_tool_result([&](const ToolCallResult& result) {
            results.push_back(result);
        });
        return results;
    }

    /// 遍历工具结果引用，避免复制大输出
    template <typename Fn>
    void for_each_tool_result(Fn&& fn) const {
        for (const auto& block : content_) {
            if (block.is_tool_result()) {
                fn(block.tool_result());
            }
        }
    }
    
private:
    Role role_ = Role::User;
    std::vector<ContentBlock> content_;
};

} // namespace ben_gear::acp
