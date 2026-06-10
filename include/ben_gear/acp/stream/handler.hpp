#pragma once

#include "ben_gear/acp/core/content_block.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"
#include <functional>

namespace ben_gear::acp {

// 使用命名空间别名简化代码
namespace container = base::container;

// ==================== 流式事件类型 ====================

enum class StreamEventType : uint8_t {
    MessageStart,       // 消息开始
    ContentBlockStart,  // 内容块开始
    ContentBlockDelta,  // 内容块增量
    ContentBlockStop,   // 内容块结束
    MessageStop,        // 消息结束
    Error               // 错误
};

// ==================== 流式事件 ====================

struct StreamEvent {
    StreamEventType type;
    int index = 0;  // 内容块索引
    container::String delta;  // 增量文本
    ContentBlock block;  // 完整内容块（仅 ContentBlockStart）
    container::String error;  // 错误信息（仅 Error）
};

// ==================== 流式处理器接口 ====================

class IStreamHandler {
public:
    virtual ~IStreamHandler() = default;
    
    /// 处理事件
    virtual void on_event(const StreamEvent& event) = 0;
    
    /// 消息开始
    virtual void on_message_start() {}
    
    /// 内容块开始
    virtual void on_content_block_start([[maybe_unused]] int index, 
                                        [[maybe_unused]] const ContentBlock& block) {}
    
    /// 内容块增量
    virtual void on_content_block_delta([[maybe_unused]] int index, 
                                        [[maybe_unused]] const container::String& delta) {}
    
    /// 内容块结束
    virtual void on_content_block_stop([[maybe_unused]] int index) {}
    
    /// 消息结束
    virtual void on_message_stop() {}
    
    /// 错误
    virtual void on_error([[maybe_unused]] const container::String& error) {}
};

// ==================== 默认流式处理器 ====================

class DefaultStreamHandler : public IStreamHandler {
public:
    void on_event(const StreamEvent& event) override {
        switch (event.type) {
            case StreamEventType::MessageStart:
                on_message_start();
                break;
            case StreamEventType::ContentBlockStart:
                on_content_block_start(event.index, event.block);
                break;
            case StreamEventType::ContentBlockDelta:
                on_content_block_delta(event.index, event.delta);
                break;
            case StreamEventType::ContentBlockStop:
                on_content_block_stop(event.index);
                break;
            case StreamEventType::MessageStop:
                on_message_stop();
                break;
            case StreamEventType::Error:
                on_error(event.error);
                break;
        }
    }
};

// ==================== 回调式流式处理器 ====================

class CallbackStreamHandler : public DefaultStreamHandler {
public:
    using TextCallback = std::function<void(const container::String&)>;
    using ThinkingCallback = std::function<void(const container::String&)>;
    using ToolCallCallback = std::function<void(const llm::ToolCallRequest&)>;
    using ToolResultCallback = std::function<void(const llm::ToolCallResult&)>;
    using ErrorCallback = std::function<void(const container::String&)>;
    
    // ==================== 设置回调 ====================
    
    void set_on_text(TextCallback callback) {
        on_text_ = std::move(callback);
    }
    
    void set_on_thinking(ThinkingCallback callback) {
        on_thinking_ = std::move(callback);
    }
    
    void set_on_tool_call(ToolCallCallback callback) {
        on_tool_call_ = std::move(callback);
    }
    
    void set_on_tool_result(ToolResultCallback callback) {
        on_tool_result_ = std::move(callback);
    }
    
    void set_on_error(ErrorCallback callback) {
        on_error_ = std::move(callback);
    }
    
    // ==================== 事件处理 ====================
    
    void on_content_block_start(int index, const ContentBlock& block) override {
        current_blocks_[index] = block;
    }
    
    void on_content_block_delta(int index, const container::String& delta) override {
        auto it = current_blocks_.find(index);
        if (it == current_blocks_.end()) return;
        
        auto& block = it->second;
        
        // 根据类型分发回调
        if (block.is_text() && on_text_) {
            on_text_(delta);
        } else if (block.is_thinking() && on_thinking_) {
            on_thinking_(delta);
        }
    }
    
    void on_content_block_stop(int index) override {
        auto it = current_blocks_.find(index);
        if (it == current_blocks_.end()) return;
        
        auto& block = it->second;
        
        // 根据类型分发回调
        if (block.is_tool_use() && on_tool_call_) {
            on_tool_call_(block.tool_use());
        } else if (block.is_tool_result() && on_tool_result_) {
            on_tool_result_(block.tool_result());
        }
        
        current_blocks_.erase(index);
    }
    
    void on_error(const container::String& error) override {
        if (on_error_) {
            on_error_(error);
        }
    }
    
private:
    container::Map<int, ContentBlock> current_blocks_;
    
    TextCallback on_text_;
    ThinkingCallback on_thinking_;
    ToolCallCallback on_tool_call_;
    ToolResultCallback on_tool_result_;
    ErrorCallback on_error_;
};

} // namespace ben_gear::acp
