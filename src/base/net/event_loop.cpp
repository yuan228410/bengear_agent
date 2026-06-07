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

// 平台 I/O 多路复用头文件（仅此文件使用）
#if BEN_GEAR_PLATFORM_LINUX
#include <sys/epoll.h>
#elif !BEN_GEAR_PLATFORM_WINDOWS
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
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
