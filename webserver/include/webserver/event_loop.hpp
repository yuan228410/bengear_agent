#pragma once

#include "webserver/core.hpp"
#include <sys/event.h>
#include <functional>
#include <unordered_map>

namespace ws {

/// IO 事件类型
enum class EventType : uint8_t {
    READ   = 0x01,
    WRITE  = 0x02,
    ERROR  = 0x04,
};

/// 事件回调：socket, event_type -> void
using EventCallback = std::function<void(Socket, EventType)>;

/// 事件循环：基于 kqueue 的异步 IO 多路复用
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// 注册 socket 事件监听
    void add_event(Socket fd, EventType type, const EventCallback& cb);

    /// 修改 socket 事件监听
    void update_event(Socket fd, EventType type, const EventCallback& cb);

    /// 删除 socket 事件监听
    void remove_event(Socket fd);

    /// 启动事件循环（阻塞运行）
    void run();

    /// 停止事件循环
    void stop() noexcept { stop_ = true; }

    /// 是否正在运行
    [[nodiscard]] bool is_running() const noexcept { return running_; }

private:
    /// 处理单个事件
    void handle_event(const struct kevent* ev);

    int kq_;                           ///< kqueue 描述符
    std::unordered_map<Socket, EventCallback> read_cbs_;
    std::unordered_map<Socket, EventCallback> write_cbs_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
};

/// EventType 转 kqueue filter
inline int event_type_to_filter(EventType type) {
    switch (type) {
        case EventType::READ:  return EVFILT_READ;
        case EventType::WRITE: return EVFILT_WRITE;
        default: return -1;
    }
}

}  // namespace ws
