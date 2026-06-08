#pragma once

#include "handler.hpp"
#include "ben_gear/base/container/vector.hpp"
#include <memory>

namespace ben_gear::acp {

// 使用命名空间别名简化代码
namespace container = base::container;

// ==================== 流式事件分发器 ====================

class StreamDispatcher {
public:
    // ==================== 处理器管理 ====================
    
    /// 添加处理器
    void add_handler(std::shared_ptr<IStreamHandler> handler) {
        handlers_.push_back(std::move(handler));
    }
    
    /// 移除处理器
    void remove_handler(std::shared_ptr<IStreamHandler> handler) {
        auto it = std::find(handlers_.begin(), handlers_.end(), handler);
        if (it != handlers_.end()) {
            handlers_.erase(it);
        }
    }
    
    /// 清空处理器
    void clear_handlers() {
        handlers_.clear();
    }
    
    // ==================== 事件分发 ====================
    
    /// 分发事件到所有处理器
    void dispatch(const StreamEvent& event) {
        for (auto& handler : handlers_) {
            handler->on_event(event);
        }
    }
    
    /// 分发消息开始事件
    void dispatch_message_start() {
        StreamEvent event;
        event.type = StreamEventType::MessageStart;
        dispatch(event);
    }
    
    /// 分发内容块开始事件
    void dispatch_content_block_start(int index, const ContentBlock& block) {
        StreamEvent event;
        event.type = StreamEventType::ContentBlockStart;
        event.index = index;
        event.block = block;
        dispatch(event);
    }
    
    /// 分发内容块增量事件
    void dispatch_content_block_delta(int index, const container::String& delta) {
        StreamEvent event;
        event.type = StreamEventType::ContentBlockDelta;
        event.index = index;
        event.delta = delta;
        dispatch(event);
    }
    
    /// 分发内容块结束事件
    void dispatch_content_block_stop(int index) {
        StreamEvent event;
        event.type = StreamEventType::ContentBlockStop;
        event.index = index;
        dispatch(event);
    }
    
    /// 分发消息结束事件
    void dispatch_message_stop() {
        StreamEvent event;
        event.type = StreamEventType::MessageStop;
        dispatch(event);
    }
    
    /// 分发错误事件
    void dispatch_error(const container::String& error) {
        StreamEvent event;
        event.type = StreamEventType::Error;
        event.error = error;
        dispatch(event);
    }
    
private:
    container::Vector<std::shared_ptr<IStreamHandler>> handlers_;
};

} // namespace ben_gear::acp
