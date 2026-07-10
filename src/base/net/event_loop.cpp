#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/concurrency/tid.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <algorithm>
#include <thread>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

// 平台 I/O 多路复用头文件（仅此文件使用）
#if BEN_GEAR_PLATFORM_LINUX
#include <sys/epoll.h>
#elif !BEN_GEAR_PLATFORM_WINDOWS
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <mswsock.h>
#endif

namespace ben_gear::net {

// ---------------------------------------------------------------------------
// IoAwaiter / TimerAwaiter 实现
// ---------------------------------------------------------------------------

void IoAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(operation_);
}

bool TimerAwaiter::await_ready() const noexcept {
    return operation_->deadline <= std::chrono::steady_clock::now();
}

void TimerAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(operation_);
}

void ReadAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(operation_);
}

void WriteAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(operation_);
}

// ---------------------------------------------------------------------------
// EventLoop::Impl — 所有内部状态集中于此
// ---------------------------------------------------------------------------

struct EventLoop::Impl {
#if !BEN_GEAR_PLATFORM_WINDOWS
    int poller = -1;                // epoll/kqueue fd
#endif
    WakeupFd wakeup;                // 跨线程唤醒机制（平台差异由 WakeupFd 封装）
    std::atomic<bool> stopped_{false};  // 停止标志
    std::atomic<int> pending_task_count_{0};  // 已提交未完成的任务计数（drain 用）
    std::atomic<uint64_t> loop_thread_id_{0};  // EventLoop 线程 ID（sync_wait 死锁检测用）

    std::unordered_map<IoOperation*, std::shared_ptr<IoOperation>> pending;
    std::vector<std::shared_ptr<TimerOperation>> timers;  // 按截止时间排序
    std::vector<std::pair<std::chrono::steady_clock::time_point, socket_handle>> close_timeouts;  // 按截止时间排序
    std::mutex mutex;

    // SIGINT/Ctrl+C 取消时立即关闭的 socket fd
    // 由 send_with_transport 设置，由 SIGINT handler 读取后 close_after(0)
    std::atomic<socket_handle> cancel_socket{invalid_socket_handle};

#if BEN_GEAR_PLATFORM_WINDOWS
    HANDLE iocp = nullptr;  // IOCP 完成端口
    std::unordered_map<OVERLAPPED*, std::shared_ptr<IoOperation>> iocp_outstanding;
    std::unordered_set<socket_handle> iocp_sockets;  // 已关联 IOCP 的 socket 集合
#endif

    // MPSC 无锁入站队列
    std::atomic<InboundOp*> inbound_head{nullptr};

    /// 入队（无锁，任意线程可调用）
    void enqueue(InboundOp* op) {
        op->next = inbound_head.load(std::memory_order_relaxed);
        while (!inbound_head.compare_exchange_weak(
                   op->next, op,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {
        }
    }

    /// 批量收割入站操作（仅 EventLoop 线程调用），返回 FIFO 顺序
    std::vector<InboundOp*> drain_inbound() {
        auto* head = inbound_head.exchange(nullptr, std::memory_order_acquire);
        if (!head) return {};

        std::vector<InboundOp*> ops;
        for (auto* cur = head; cur; cur = cur->next) {
            ops.push_back(cur);
        }
        std::reverse(ops.begin(), ops.end());
        return ops;
    }
};

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

EventLoop::EventLoop() : impl_(std::make_unique<Impl>()) {
#if BEN_GEAR_PLATFORM_LINUX
    impl_->poller = epoll_create1(EPOLL_CLOEXEC);
    if (impl_->poller < 0) {
        throw std::runtime_error("epoll_create1 failed");
    }
    // 注册 wakeup fd 到 epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = impl_->wakeup.read_fd();
    epoll_ctl(impl_->poller, EPOLL_CTL_ADD, impl_->wakeup.read_fd(), &ev);
#elif !BEN_GEAR_PLATFORM_WINDOWS
    impl_->poller = kqueue();
    if (impl_->poller < 0) {
        throw std::runtime_error("kqueue failed");
    }
    // 注册 wakeup fd 到 kqueue
    struct kevent ev{};
    EV_SET(&ev, impl_->wakeup.read_fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(impl_->poller, &ev, 1, nullptr, 0, nullptr);
#endif

#if BEN_GEAR_PLATFORM_WINDOWS
    impl_->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!impl_->iocp) {
        throw std::runtime_error("CreateIoCompletionPort failed");
    }
#endif
}

EventLoop::~EventLoop() {
    stop();

    // 排空并删除残留入站操作
    auto ops = impl_->drain_inbound();
    for (auto* op : ops) {
        delete op;
    }

#if BEN_GEAR_PLATFORM_POSIX
    if (impl_->poller >= 0) {
        ::close(impl_->poller);
    }
#endif

#if BEN_GEAR_PLATFORM_WINDOWS
    if (impl_->iocp) {
        CloseHandle(impl_->iocp);
        impl_->iocp = nullptr;
    }
#endif

    // wakeup fd 由 WakeupFd 析构函数自动关闭
}

// ---------------------------------------------------------------------------
// submit — 无锁入队
// ---------------------------------------------------------------------------

void EventLoop::submit(std::shared_ptr<IoOperation> operation) {
    auto* op = new InboundOp{InboundOp::Tag::io, std::move(operation), nullptr, nullptr, nullptr};
    impl_->enqueue(op);
}

void EventLoop::submit(std::shared_ptr<TimerOperation> operation) {
    auto* op = new InboundOp{InboundOp::Tag::timer, nullptr, std::move(operation), nullptr, nullptr};
    impl_->enqueue(op);
}

void EventLoop::submit_task(std::function<void()> func) {
    impl_->pending_task_count_.fetch_add(1, std::memory_order_relaxed);
    auto* op = new InboundOp{InboundOp::Tag::task, nullptr, nullptr, std::move(func), nullptr};
    impl_->enqueue(op);
    wakeup();
}

// ---------------------------------------------------------------------------
// close_after / cancel_close
// ---------------------------------------------------------------------------

void EventLoop::close_after(socket_handle fd, std::chrono::milliseconds delay) {
    std::lock_guard lock(impl_->mutex);
    auto deadline = std::chrono::steady_clock::now() + delay;
    auto it = std::lower_bound(impl_->close_timeouts.begin(), impl_->close_timeouts.end(), deadline,
        [](const auto& entry, const auto& dl) { return entry.first < dl; });
    impl_->close_timeouts.insert(it, {deadline, fd});
}

void EventLoop::cancel_close(socket_handle fd) {
    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->close_timeouts.begin(); it != impl_->close_timeouts.end(); ++it) {
        if (it->second == fd) {
            impl_->close_timeouts.erase(it);
            return;
        }
    }
}

void EventLoop::set_cancel_socket(socket_handle fd) {
    impl_->cancel_socket.store(fd, std::memory_order_release);
}

socket_handle EventLoop::get_cancel_socket() const {
    return impl_->cancel_socket.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// wakeup / stop
// ---------------------------------------------------------------------------

void EventLoop::wakeup() {
    impl_->wakeup.notify();
}

bool EventLoop::is_loop_thread() const {
    auto loop_tid = impl_->loop_thread_id_.load(std::memory_order_acquire);
    return loop_tid != 0 && loop_tid == base::concurrency::current_thread_id();
}

void EventLoop::drain(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    // 等待所有已提交的任务执行完毕，带超时保护
    while (impl_->pending_task_count_.load(std::memory_order_acquire) > 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            auto remaining = impl_->pending_task_count_.load(std::memory_order_relaxed);
            log::warn_fmt("EventLoop::drain() timed out after {}ms, {} tasks still pending",
                          timeout.count(), remaining);
            break;
        }
        std::this_thread::yield();
    }
    // 再跑一次 run_once 确保入站队列中的 I/O 操作也被处理
    run_once(std::chrono::milliseconds{10});
    // 停止
    impl_->stopped_.store(true, std::memory_order_release);
    wakeup();
}

void EventLoop::stop() {
    impl_->stopped_.store(true, std::memory_order_release);
    wakeup();
}

// ---------------------------------------------------------------------------
// run_once — 事件循环核心
// ---------------------------------------------------------------------------

namespace {

std::chrono::milliseconds next_timeout(std::chrono::milliseconds requested, const std::vector<std::shared_ptr<TimerOperation>>& timers) {
    if (timers.empty()) {
        return requested;
    }
    const auto now = std::chrono::steady_clock::now();
    if (timers.front()->deadline <= now) {
        return std::chrono::milliseconds{0};
    }
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(timers.front()->deadline - now);
    return std::min(requested, std::max(std::chrono::milliseconds{1}, wait));
}

}  // namespace

void EventLoop::run_once(std::chrono::milliseconds timeout) {
    // Phase 1: 排空入站队列 + 处理
    // task_func（协程 resume）必须在锁外执行（避免 close_after 等死锁）
    // 协程 resume 后可能产生新的入站操作，循环 drain 确保同一次 run_once 处理
    for (;;) {
        auto inbound = impl_->drain_inbound();
        if (inbound.empty()) break;

        std::vector<std::function<void()>> pending_tasks;
        {
            std::lock_guard lock(impl_->mutex);
            for (auto* op : inbound) {
                switch (op->tag) {
                case InboundOp::Tag::io:
#if BEN_GEAR_PLATFORM_LINUX
                    {
                        epoll_event event{};
                        event.events = EPOLLONESHOT | (op->io->event == IoEvent::read ? EPOLLIN : EPOLLOUT);
                        auto* raw = op->io.get();
                        event.data.ptr = raw;
                        if (epoll_ctl(impl_->poller, EPOLL_CTL_ADD, raw->socket, &event) < 0) {
                            if (errno != EEXIST || epoll_ctl(impl_->poller, EPOLL_CTL_MOD, raw->socket, &event) < 0) {
                                delete op;
                                continue;
                            }
                        }
                        impl_->pending[raw] = std::move(op->io);
                    }
#elif !BEN_GEAR_PLATFORM_WINDOWS
                    {
                        struct kevent change{};
                        auto* raw = op->io.get();
                        EV_SET(&change,
                               raw->socket,
                               raw->event == IoEvent::read ? EVFILT_READ : EVFILT_WRITE,
                               EV_ADD | EV_ENABLE | EV_ONESHOT,
                               0,
                               0,
                               raw);
                        if (kevent(impl_->poller, &change, 1, nullptr, 0, nullptr) < 0) {
                            delete op;
                            continue;
                        }
                        impl_->pending[raw] = std::move(op->io);
                    }
#else
                    // Windows: 区分 IOCP 传输操作和就绪模式操作
                    {
                        auto* raw = op->io.get();
                        if (raw->transfer_buf) {
                            // --- IOCP 完成模式（read_some / write_some） ---
                            if (!impl_->iocp_sockets.count(raw->socket)) {
                                if (!CreateIoCompletionPort((HANDLE)raw->socket, impl_->iocp, 0, 0)) {
                                    delete op;
                                    continue;
                                }
                                impl_->iocp_sockets.insert(raw->socket);
                            }

                            OVERLAPPED* ov = &raw->overlapped;
                            memset(ov, 0, sizeof(OVERLAPPED));

                            WSABUF buf = {raw->transfer_len, raw->transfer_buf};
                            DWORD flags = 0;
                            DWORD bytes_transferred = 0;
                            int rc;
                            if (raw->event == IoEvent::read) {
                                rc = WSARecv(raw->socket, &buf, 1, &bytes_transferred, &flags, ov, nullptr);
                            } else {
                                rc = WSASend(raw->socket, &buf, 1, &bytes_transferred, 0, ov, nullptr);
                            }

                            if (rc == 0) {
                                // 同步完成：IOCP 不会投递完成包，手动投递
                                raw->transfer_result = bytes_transferred;
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                                PostQueuedCompletionStatus(impl_->iocp, bytes_transferred, 0, ov);
                            } else if (WSAGetLastError() == WSA_IO_PENDING) {
                                // 异步等待 IOCP 完成
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                            } else {
                                // 同步失败，用 PostQueuedCompletionStatus 触发错误处理
                                raw->error_code = WSAGetLastError();
                                raw->transfer_result = 0;
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                                PostQueuedCompletionStatus(impl_->iocp, 0, 0, ov);
                            }
                        } else {
                            // --- 就绪模式（wait_read / wait_write，select 回退） ---
                            auto* raw = op->io.get();
                            impl_->pending[raw] = std::move(op->io);
                        }
                    }
#endif
                    break;

                case InboundOp::Tag::timer:
                    {
                        auto it = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), op->timer->deadline,
                            [](const std::shared_ptr<TimerOperation>& t, const std::chrono::steady_clock::time_point& dl) {
                                return t->deadline < dl;
                            });
                        impl_->timers.insert(it, std::move(op->timer));
                    }
                    break;

                case InboundOp::Tag::task:
                    if (op->task_func) {
                        pending_tasks.push_back(std::move(op->task_func));
                    }
                    break;
                }
                delete op;
            }
        }
        // 锁外执行 task_func（协程 resume）
        for (auto& task : pending_tasks) {
            task();
            impl_->pending_task_count_.fetch_sub(1, std::memory_order_relaxed);
        }
        // 协程 resume 可能产生新的入站操作，循环 drain
    }

    // Phase 2: 计算 poller 超时（考虑定时器截止时间）
    {
        std::lock_guard lock(impl_->mutex);
        timeout = next_timeout(timeout, impl_->timers);
    }

    // Phase 3: poller wait + 处理就绪事件
#if BEN_GEAR_PLATFORM_LINUX
    {
        epoll_event events[64]{};
        const int count = epoll_wait(impl_->poller, events, 64, static_cast<int>(timeout.count()));
        {
            std::vector<std::shared_ptr<IoOperation>> to_resume;
            {
                std::lock_guard lock(impl_->mutex);
                for (int index = 0; index < count; ++index) {
                    if (events[index].data.fd == impl_->wakeup.read_fd()) {
                        continue;
                    }
                    auto* raw = static_cast<IoOperation*>(events[index].data.ptr);
                    auto it = impl_->pending.find(raw);
                    if (it == impl_->pending.end()) {
                        continue;
                    }
                    to_resume.push_back(std::move(it->second));
                    impl_->pending.erase(it);
                }
            }
            for (auto& operation : to_resume) {
                operation->continuation.resume();
            }
        }
        impl_->wakeup.drain();
    }
#elif !BEN_GEAR_PLATFORM_WINDOWS
    {
        struct kevent events[64]{};
        timespec time{};
        time.tv_sec = static_cast<time_t>(timeout.count() / 1000);
        time.tv_nsec = static_cast<long>((timeout.count() % 1000) * 1000000);
        const int count = kevent(impl_->poller, nullptr, 0, events, 64, &time);
        {
            std::vector<std::shared_ptr<IoOperation>> to_resume;
            {
                std::lock_guard lock(impl_->mutex);
                for (int index = 0; index < count; ++index) {
                    if (events[index].udata == nullptr) {
                        continue;
                    }
                    auto* raw = static_cast<IoOperation*>(events[index].udata);
                    auto it = impl_->pending.find(raw);
                    if (it == impl_->pending.end()) {
                        continue;
                    }
                    to_resume.push_back(std::move(it->second));
                    impl_->pending.erase(it);
                }
            }
            for (auto& operation : to_resume) {
                operation->continuation.resume();
            }
        }
        impl_->wakeup.drain();
    }
#else
    // Windows: IOCP + select 混合等待
    {
        std::vector<std::shared_ptr<IoOperation>> to_resume;
        bool has_iocp = !impl_->iocp_outstanding.empty();

        // --- Step A: IOCP 完成事件 ---
        if (has_iocp) {
            OVERLAPPED* ov = nullptr;
            ULONG_PTR completion_key = 0;
            DWORD bytes = 0;

            BOOL ok = GetQueuedCompletionStatus(
                impl_->iocp, &bytes, &completion_key, &ov,
                static_cast<DWORD>(timeout.count()));

            if (ov) {
                auto it = impl_->iocp_outstanding.find(ov);
                if (it != impl_->iocp_outstanding.end()) {
                    auto op = it->second;
                    impl_->iocp_outstanding.erase(it);

                    if (!op->cancelled) {
                        if (ok) {
                            op->transfer_result = bytes;
                        } else if (op->error_code == 0) {
                            DWORD err = GetLastError();
                            if (err == ERROR_SUCCESS) {
                                op->transfer_result = bytes;
                            } else {
                                op->error_code = err;
                                op->transfer_result = 0;
                            }
                        }
                        to_resume.push_back(std::move(op));
                    }
                }
            }

            if (!to_resume.empty()) {
                // 先恢复 IOCP 完成协程，再继续处理就绪模式操作
                for (auto& operation : to_resume) {
                    operation->continuation.resume();
                }
                to_resume.clear();
                // IOCP 协程恢复后可能注册新操作到入站队列
                // 新操作会由下一次 run_once 的 Phase 1 处理
                // 继续走 select 路径处理 pending 中的就绪模式操作
            }
        }

        // --- Step B: 就绪模式（select 回退） ---
        {
            fd_set read_fds, write_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);

            SOCKET max_sock = 0;
            bool has_pending = false;

            {
                std::lock_guard lock(impl_->mutex);
                for (auto& [raw, op] : impl_->pending) {
                    auto sock = op->socket;
                    if (op->event == IoEvent::read) {
                        FD_SET(sock, &read_fds);
                    } else {
                        FD_SET(sock, &write_fds);
                    }
                    if (sock > max_sock) max_sock = sock;
                    has_pending = true;
                }
            }

            if (has_pending) {
                timeval tv{};
                tv.tv_sec = static_cast<long>(timeout.count() / 1000);
                tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

                const int count = select(static_cast<int>(max_sock + 1), &read_fds, &write_fds, nullptr, &tv);

                if (count > 0) {
                    {
                        std::lock_guard lock(impl_->mutex);
                        auto it = impl_->pending.begin();
                        while (it != impl_->pending.end()) {
                            auto& op = it->second;
                            bool ready = false;
                            if (op->event == IoEvent::read && FD_ISSET(op->socket, &read_fds)) {
                                ready = true;
                            } else if (op->event == IoEvent::write && FD_ISSET(op->socket, &write_fds)) {
                                ready = true;
                            }
                            if (ready) {
                                to_resume.push_back(std::move(it->second));
                                it = impl_->pending.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    for (auto& operation : to_resume) {
                        operation->continuation.resume();
                    }
                }
            } else if (!has_iocp) {
                ::Sleep(static_cast<DWORD>(timeout.count()));
            }
        }
        impl_->wakeup.drain();
    }
#endif

    // Phase 4: 处理过期定时器
    {
        std::vector<std::shared_ptr<TimerOperation>> expired_timers;
        {
            std::lock_guard lock(impl_->mutex);
            const auto now = std::chrono::steady_clock::now();
            auto boundary = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), now,
                [](const std::shared_ptr<TimerOperation>& timer, const std::chrono::steady_clock::time_point& t) {
                    return timer->deadline < t;
                });
            if (boundary != impl_->timers.begin()) {
                expired_timers.assign(std::make_move_iterator(impl_->timers.begin()),
                                      std::make_move_iterator(boundary));
                impl_->timers.erase(impl_->timers.begin(), boundary);
            }
        }
        for (auto& operation : expired_timers) {
            operation->continuation.resume();
        }
    }

    // Phase 5: 关闭超时 fd（收集待关闭 fd，锁外执行 close_socket）
    {
        std::vector<socket_handle> fds_to_close;
        std::vector<std::shared_ptr<IoOperation>> to_resume;
        {
            std::lock_guard lock(impl_->mutex);
            const auto now = std::chrono::steady_clock::now();
            auto boundary = std::lower_bound(impl_->close_timeouts.begin(), impl_->close_timeouts.end(), now,
                [](const auto& entry, const auto& t) { return entry.first < t; });
            for (auto it = impl_->close_timeouts.begin(); it != boundary; ++it) {
                fds_to_close.push_back(it->second);
                for (auto pit = impl_->pending.begin(); pit != impl_->pending.end(); ) {
                    if (pit->first->socket == it->second) {
                        pit->second->cancelled = true;
                        to_resume.push_back(std::move(pit->second));
                        pit = impl_->pending.erase(pit);
                    } else {
                        ++pit;
                    }
                }
            }
            impl_->close_timeouts.erase(impl_->close_timeouts.begin(), boundary);
        }
        // 锁外关闭 fd（系统调用不应持锁）
        for (auto fd : fds_to_close) {
            close_socket(fd);
        }
        for (auto& op : to_resume) {
            op->continuation.resume();
        }
    }
}

// ---------------------------------------------------------------------------
// run — 长驻模式
// ---------------------------------------------------------------------------

void EventLoop::run() {
    // 记录 EventLoop 线程 ID（用于 sync_wait 死锁检测）
    // 只在 run() 长驻模式中记录，run_once 可能被任意线程临时调用
    impl_->loop_thread_id_.store(base::concurrency::current_thread_id(), std::memory_order_release);
    while (!impl_->stopped_.load(std::memory_order_acquire)) {
        run_once();
    }
}

}  // namespace ben_gear::net
