#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"

#include <string>
#include <vector>
#include <optional>

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;

/// 消息角色
enum class MessageRole {
    system,
    user,
    assistant,
    tool
};

/// 消息内容块（支持多模态）
struct ContentBlock {
    container::String type;  // "text", "image", "tool_use", "tool_result"
    std::optional<container::String> text;
    std::optional<Json> data;
    
    static ContentBlock text_block(const container::String& content) {
        ContentBlock block;
        block.type = container::String("text");
        block.text = content;
        return block;
    }
    
    static ContentBlock tool_use_block(const ToolCallRequest& call) {
        ContentBlock block;
        block.type = container::String("tool_use");
        block.data = Json{
            {"id", call.id},
            {"name", call.name},
            {"input", call.arguments}
        };
        return block;
    }
    
    static ContentBlock tool_result_block(const ToolCallResult& result) {
        ContentBlock block;
        block.type = container::String("tool_result");
        block.data = Json{
            {"tool_use_id", result.tool_call_id},
            {"content", result.output}
        };
        return block;
    }
    
    Json to_json() const {
        Json j = {{"type", type}};
        if (text) j["text"] = *text;
        if (data) {
            for (auto it = data->begin(); it != data->end(); ++it) {
                j[it.key()] = it.value();
            }
        }
        return j;
    }
};

/// 统一消息格式
struct Message {
    MessageRole role;
    container::String content;  // 简单文本内容
    container::Vector<ContentBlock> blocks;  // 复杂内容块（工具调用等）
    std::optional<container::String> name;  // 工具名称（tool 角色时）
    std::optional<container::String> tool_call_id;  // 工具调用 ID
    
    /// 创建系统消息
    static Message system(const container::String& content) {
        Message msg;
        msg.role = MessageRole::system;
        msg.content = content;
        return msg;
    }
    
    /// 创建用户消息
    static Message user(const container::String& content) {
        Message msg;
        msg.role = MessageRole::user;
        msg.content = content;
        return msg;
    }
    
    /// 创建助手消息
    static Message assistant(const container::String& content) {
        Message msg;
        msg.role = MessageRole::assistant;
        msg.content = content;
        return msg;
    }
    
    /// 创建工具结果消息
    static Message tool_result(const container::String& tool_call_id, 
                               const container::String& tool_name,
                               const container::String& result) {
        Message msg;
        msg.role = MessageRole::tool;
        msg.content = result;
        msg.name = tool_name;
        msg.tool_call_id = tool_call_id;
        return msg;
    }
    
    /// 转换为 OpenAI 格式
    Json to_openai_format() const {
        Json j;
        
        switch (role) {
            case MessageRole::system:
                j["role"] = "system";
                j["content"] = content;
                break;
            case MessageRole::user:
                j["role"] = "user";
                j["content"] = content;
                break;
            case MessageRole::assistant:
                j["role"] = "assistant";
                if (!blocks.empty()) {
                    j["content"] = content;
                    Json tool_calls = Json::array();
                    for (const auto& block : blocks) {
                        if (std::string_view(block.type) == "tool_use" && block.data) {
                            tool_calls.push_back(Json{
                                {"id", (*block.data)["id"]},
                                {"type", "function"},
                                {"function", {
                                    {"name", (*block.data)["name"]},
                                    {"arguments", (*block.data)["input"].dump()}
                                }}
                            });
                        }
                    }
                    if (!tool_calls.empty()) {
                        j["tool_calls"] = tool_calls;
                    }
                } else {
                    j["content"] = content;
                }
                break;
            case MessageRole::tool:
                j["role"] = "tool";
                j["tool_call_id"] = *tool_call_id;
                j["content"] = content;
                break;
        }
        
        return j;
    }
    
    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const {
        Json j;
        
        switch (role) {
            case MessageRole::system:
                // Anthropic 的 system 在请求顶层，不在 messages 中
                j["role"] = "system";
                j["content"] = content;
                break;
            case MessageRole::user:
                j["role"] = "user";
                if (!blocks.empty()) {
                    j["content"] = Json::array();
                    for (const auto& block : blocks) {
                        j["content"].push_back(block.to_json());
                    }
                } else {
                    j["content"] = content;
                }
                break;
            case MessageRole::assistant:
                j["role"] = "assistant";
                if (!blocks.empty()) {
                    j["content"] = Json::array();
                    for (const auto& block : blocks) {
                        j["content"].push_back(block.to_json());
                    }
                } else {
                    j["content"] = Json::array({{{"type", "text"}, {"text", content}}});
                }
                break;
            case MessageRole::tool:
                j["role"] = "user";
                j["content"] = Json::array({
                    {
                        {"type", "tool_result"},
                        {"tool_use_id", std::string(tool_call_id->c_str())},
                        {"content", content}
                    }
                });
                break;
        }
        
        return j;
    }
};

/// 对话历史管理
class ConversationHistory {
public:
    /// 添加消息
    void add_message(const Message& message) {
        messages_.push_back(message);
    }
    
    /// 添加系统消息
    void add_system(const container::String& content) {
        messages_.push_back(Message::system(content));
    }
    
    /// 添加用户消息
    void add_user(const container::String& content) {
        messages_.push_back(Message::user(content));
    }
    
    /// 添加助手消息
    void add_assistant(const container::String& content) {
        messages_.push_back(Message::assistant(content));
    }
    
    /// 添加工具结果
    void add_tool_result(const container::String& tool_call_id,
                         const container::String& tool_name,
                         const container::String& result) {
        messages_.push_back(Message::tool_result(tool_call_id, tool_name, result));
    }
    
    /// 清空历史
    void clear() {
        messages_.clear();
    }
    
    /// 获取消息列表
    const container::Vector<Message>& messages() const noexcept {
        return messages_;
    }
    
    /// 转换为 OpenAI 格式
    Json to_openai_messages() const {
        Json msgs = Json::array();
        for (const auto& msg : messages_) {
            if (msg.role != MessageRole::system) {  // OpenAI 的 system 在 messages 中
                msgs.push_back(msg.to_openai_format());
            }
        }
        return msgs;
    }
    
    /// 转换为 Anthropic 格式（不含 system）
    Json to_anthropic_messages() const {
        Json msgs = Json::array();
        for (const auto& msg : messages_) {
            if (msg.role != MessageRole::system) {  // Anthropic 的 system 在请求顶层
                msgs.push_back(msg.to_anthropic_format());
            }
        }
        return msgs;
    }
    
    /// 获取系统提示（Anthropic 用）
    container::String get_system_prompt() const {
        for (const auto& msg : messages_) {
            if (msg.role == MessageRole::system) {
                return msg.content;
            }
        }
        return container::String();
    }
    
    /// 是否为空
    bool empty() const noexcept {
        return messages_.empty();
    }
    
    /// 消息数量
    std::size_t size() const noexcept {
        return messages_.size();
    }
    
private:
    container::Vector<Message> messages_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using Message = llm::Message;
using MessageRole = llm::MessageRole;
using ConversationHistory = llm::ConversationHistory;
}  // namespace ben_gear
