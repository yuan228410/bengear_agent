#pragma once

#include "ben_gear/base/platform/os.hpp"

namespace ben_gear::net {

/// 跨线程唤醒机制（跨平台封装）
///
/// 封装 EventLoop 的跨线程唤醒机制：
/// - Linux: eventfd（轻量，单 fd）
/// - macOS/POSIX: pipe（通用兼容）
/// - Windows: WSAEventSelect + socketpair（WSA 事件机制）
///
/// 设计原则：
/// - 所有平台差异收敛在此文件 + wakeup_fd.cpp
/// - EventLoop 和 IoContext 不直接使用 #if 平台宏
/// - RAII 管理 fd 生命周期
///
/// 使用示例：
/// ```cpp
/// WakeupFd wakeup;
/// wakeup.notify();           // 从任意线程调用，唤醒 EventLoop
/// wakeup.drain();            // 在 EventLoop 线程消耗唤醒信号
/// wakeup.read_fd();          // 获取读取端 fd，用于注册到 epoll/kqueue/WSA
/// ```
class WakeupFd {
public:
    WakeupFd();
    ~WakeupFd();

    WakeupFd(const WakeupFd&) = delete;
    WakeupFd& operator=(const WakeupFd&) = delete;

    /// 唤醒 EventLoop（线程安全）
    void notify();

    /// 消耗唤醒信号（仅在 EventLoop 线程调用）
    void drain();

    /// 获取读取端 fd（用于注册到 epoll/kqueue）
    /// Linux: 返回 eventfd
    /// macOS: 返回 pipe[0]
    /// Windows: 返回 -1（Windows 使用 WSAEventSelect，不通过 fd 注册，
    ///          EventLoop 的 Windows 实现需使用 WSAEventWait 替代 poller）
    int read_fd() const;

    /// 是否有效
    bool valid() const;

private:
#if BEN_GEAR_PLATFORM_WINDOWS
    SOCKET read_sock_ = INVALID_SOCKET;
    SOCKET write_sock_ = INVALID_SOCKET;
    WSAEVENT wsa_event_ = WSA_INVALID_EVENT;
#elif BEN_GEAR_PLATFORM_LINUX
    int fd_ = -1;           // eventfd
#else
    int pipe_[2] = {-1, -1}; // pipe[0]=read, pipe[1]=write
#endif
};

}  // namespace ben_gear::net