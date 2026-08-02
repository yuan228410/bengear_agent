#include "net/event_loop.hpp"
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <condition_variable>
#include "platform/os.hpp"
#include "concurrency/tid.hpp"
#include "log/logger.hpp"

#include <algorithm>
#include <thread>
#include <cerrno>
#include <stdexcept>
#include <system_error>
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
    loop_.submit(std::move(operation_));
}

void TimerAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(std::move(operation_));
}

void ReadAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(std::move(operation_));
}

void WriteAwaiter::await_suspend(std::coroutine_handle<> handle) {
    operation_->continuation = handle;
    loop_.submit(std::move(operation_));
}

// ---------------------------------------------------------------------------
// EventLoop::Impl — 所有内部状态集中于此
// ---------------------------------------------------------------------------

struct EventLoop::Impl {
    // I/O 操作对象池：避免每次 await / submit 堆分配。
    // 声明在所有持有 IoOpPtr/TimerOpPtr/InboundOp 的成员之前，
    // 确保在这些 unique_ptr 析构（归还对象到池）之后，池本身才被析构。
    template <typename T>
    struct ObjectPool {
        std::mutex m;
        std::vector<T*> free_;
        ~ObjectPool() { for (auto* p : free_) delete p; }
    };
    ObjectPool<IoOperation> io_pool;
    ObjectPool<TimerOperation> timer_pool;
    ObjectPool<InboundOp> inbound_pool;

#if !BEN_GEAR_PLATFORM_WINDOWS
    int poller = -1;                // epoll/kqueue fd
#endif
    WakeupFd wakeup;                // 跨线程唤醒机制（平台差异由 WakeupFd 封装）
    std::atomic<bool> stopped_{false};  // 停止标志
    std::atomic<int> pending_task_count_{0};  // 已提交未完成的任务计数（drain 用）
    std::atomic<uint64_t> loop_thread_id_{0};  // EventLoop 线程 ID（sync_wait 死锁检测用）

    // loop 线程运行状态：drain() 依赖它在 loop 线程退出 run_once 后才执行收尾，
    // 避免 drain 线程与 loop 线程并发执行 run_once（数据竞争 / 协程双重 resume）
    std::atomic<bool> loop_running_{false};
    std::mutex loop_exit_mutex;
    std::condition_variable loop_exit_cv;

    std::unordered_map<IoOperation*, IoOpPtr> pending;
    std::vector<TimerOpPtr> timers;  // 按截止时间排序
    std::vector<std::pair<std::chrono::steady_clock::time_point, socket_handle>> close_timeouts;  // 按截止时间排序
    std::mutex mutex;

    // drain 条件变量：替代 yield() 忙等
    std::mutex drain_mutex;
    std::condition_variable drain_cv;

    // SIGINT/Ctrl+C 取消时立即关闭的 socket fd
    // 由 send_with_transport 设置，由 SIGINT handler 读取后 close_after(0)
    std::atomic<socket_handle> cancel_socket{invalid_socket_handle};

#if BEN_GEAR_PLATFORM_WINDOWS
    HANDLE iocp = nullptr;  // IOCP 完成端口
    std::unordered_map<OVERLAPPED*, IoOpPtr> iocp_outstanding;
    std::unordered_set<socket_handle> iocp_sockets;  // 已关联 IOCP 的 socket 集合
#endif

    // MPSC 无锁入站队列
    std::atomic<InboundOp*> inbound_head{nullptr};

    /// 入队（无锁，任意线程可调用）
    void enqueue(InboundOp* op) {
        op->next = inbound_head.load(std::memory_order_relaxed);
        int spin = 0;
        while (!inbound_head.compare_exchange_weak(
                   op->next, op,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {
            // CAS 失败时 PAUSE 退避，减少缓存行乒乓
            if (++spin < 16) {
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86) || defined(_M_X86)
#if defined(_MSC_VER)
                _mm_pause();
#else
                __builtin_ia32_pause();
#endif
#endif
            }
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

// ---------------------------------------------------------------------------
// 对象池：IoOperation / TimerOperation / InboundOp 复用，热路径免堆分配
// ---------------------------------------------------------------------------

// IoOpDeleter / TimerOpDeleter operator() 定义：归还对象到对象池
void IoOpDeleter::operator()(IoOperation* op) {
    auto* pool = static_cast<EventLoop::Impl*>(pool_ctx);
    std::lock_guard<std::mutex> lock(pool->io_pool.m);
    pool->io_pool.free_.push_back(op);
}

void TimerOpDeleter::operator()(TimerOperation* op) {
    auto* pool = static_cast<EventLoop::Impl*>(pool_ctx);
    std::lock_guard<std::mutex> lock(pool->timer_pool.m);
    pool->timer_pool.free_.push_back(op);
}

InboundOp* EventLoop::acquire_inbound(Impl& impl) {
    InboundOp* op = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl.inbound_pool.m);
        if (!impl.inbound_pool.free_.empty()) {
            op = impl.inbound_pool.free_.back();
            impl.inbound_pool.free_.pop_back();
        }
    }
    if (!op) op = new InboundOp{};
    op->next = nullptr;
    op->io.reset();
    op->timer.reset();
    op->task_func = nullptr;
    return op;
}

void EventLoop::recycle_inbound(InboundOp* op) {
    op->io.reset();
    op->timer.reset();
    op->task_func = nullptr;
    op->next = nullptr;
    std::lock_guard<std::mutex> lock(impl_->inbound_pool.m);
    impl_->inbound_pool.free_.push_back(op);
}

IoOpPtr acquire_io(EventLoop& loop) {
    auto* impl = loop.impl_.get();
    IoOperation* raw = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl->io_pool.m);
        if (!impl->io_pool.free_.empty()) {
            raw = impl->io_pool.free_.back();
            impl->io_pool.free_.pop_back();
        }
    }
    if (!raw) raw = new IoOperation();
    *raw = IoOperation{};  // 复位为默认状态（OVERLAPPED 等由调用方按需设置）
    return IoOpPtr(raw, IoOpDeleter{impl});
}

TimerOpPtr acquire_timer(EventLoop& loop) {
    auto* impl = loop.impl_.get();
    TimerOperation* raw = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl->timer_pool.m);
        if (!impl->timer_pool.free_.empty()) {
            raw = impl->timer_pool.free_.back();
            impl->timer_pool.free_.pop_back();
        }
    }
    if (!raw) raw = new TimerOperation{};
    *raw = TimerOperation{};
    return TimerOpPtr(raw, TimerOpDeleter{impl});
}

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
        recycle_inbound(op);
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

void EventLoop::submit(IoOpPtr operation) {
    auto* op = acquire_inbound(*impl_);
    op->tag = InboundOp::Tag::io;
    op->io = std::move(operation);
    impl_->enqueue(op);
    // 唤醒 loop：可能从非 loop 线程提交，不唤醒会等到 poll 超时（最多 100ms）才处理
    wakeup();
}

void EventLoop::submit(TimerOpPtr operation) {
    auto* op = acquire_inbound(*impl_);
    op->tag = InboundOp::Tag::timer;
    op->timer = std::move(operation);
    impl_->enqueue(op);
    wakeup();
}

void EventLoop::submit_task(std::function<void()> func) {
    impl_->pending_task_count_.fetch_add(1, std::memory_order_relaxed);
    auto* op = acquire_inbound(*impl_);
    op->tag = InboundOp::Tag::task;
    op->task_func = std::move(func);
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

void EventLoop::on_socket_closed(socket_handle fd) {
    std::vector<IoOpPtr> to_resume;
    {
        std::lock_guard lock(impl_->mutex);
        // 清除 close_timeouts 中该 fd 的条目，避免 fd 被显式关闭后 Phase 5 再次
        // close_socket 落到已复用（相同句柄值）的新 socket 上
        impl_->close_timeouts.erase(
            std::remove_if(impl_->close_timeouts.begin(), impl_->close_timeouts.end(),
                [fd](const auto& entry) { return entry.second == fd; }),
            impl_->close_timeouts.end());
        // 取消该 fd 上就绪模式的挂起操作（select/WSAPoll 无内核完成包，
        // 不取消则协程永久悬挂、fire_and_forget 任务泄漏）
        for (auto it = impl_->pending.begin(); it != impl_->pending.end(); ) {
            if (it->second->socket == fd) {
                it->second->cancelled = true;
                to_resume.push_back(std::move(it->second));
                it = impl_->pending.erase(it);
            } else {
                ++it;
            }
        }
#if BEN_GEAR_PLATFORM_WINDOWS
        // 清除 IOCP 关联缓存：必须在 fd 关闭（句柄值可复用）之前完成，
        // 否则新 socket 复用同一句柄值时会跳过 CreateIoCompletionPort 关联（Bug 5）
        impl_->iocp_sockets.erase(fd);
#endif
    }
    // 锁外 resume：await_resume 观察到 cancelled 抛超时异常
    for (auto& op : to_resume) {
        op->continuation.resume();
    }
}

// ---------------------------------------------------------------------------
// wakeup / stop
// ---------------------------------------------------------------------------

void EventLoop::wakeup() {
    impl_->wakeup.notify();
#if BEN_GEAR_PLATFORM_WINDOWS
    // Windows: 通过 IOCP 完成端口唤醒 GQCS 等待（PostQueuedCompletionStatus 可中断 GQCS）
    if (impl_->iocp) {
        PostQueuedCompletionStatus(impl_->iocp, 0, 0, nullptr);
    }
#endif
}

bool EventLoop::is_loop_thread() const {
    auto loop_tid = impl_->loop_thread_id_.load(std::memory_order_acquire);
    return loop_tid != 0 && loop_tid == base::concurrency::current_thread_id();
}

void EventLoop::drain(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    {
        std::unique_lock lock(impl_->drain_mutex);
        impl_->drain_cv.wait_until(lock, deadline, [this] {
            return impl_->pending_task_count_.load(std::memory_order_acquire) == 0;
        });
    }
    // 超时后仍有未完成任务，打日志但不阻塞
    if (impl_->pending_task_count_.load(std::memory_order_acquire) > 0) {
        auto remaining = impl_->pending_task_count_.load(std::memory_order_relaxed);
        log::warn_fmt("EventLoop::drain() timed out after {}ms, {} tasks still pending",
                      timeout.count(), remaining);
    }
    // 停止 loop 线程，并等待它退出当前 run_once，避免与 drain 线程并发执行 run_once
    impl_->stopped_.store(true, std::memory_order_release);
    wakeup();
    if (impl_->loop_running_.load(std::memory_order_acquire)) {
        std::unique_lock lock(impl_->loop_exit_mutex);
        impl_->loop_exit_cv.wait_until(lock, deadline, [this] {
            return !impl_->loop_running_.load(std::memory_order_acquire);
        });
    }
    if (impl_->loop_running_.load(std::memory_order_acquire)) {
        // loop 线程仍未退出，不能并发 run_once（GQCS/WSAPoll 与协程 resume 会竞争）
        log::warn_fmt("EventLoop::drain() timed out waiting for loop thread to stop");
        return;
    }
    // 再跑一次 run_once 确保入站队列中的 I/O 操作也被处理（此时 loop 已停，单线程安全）
    run_once(std::chrono::milliseconds{10});
}

void EventLoop::stop() {
    impl_->stopped_.store(true, std::memory_order_release);
    wakeup();
}

void EventLoop::reset_stop() {
    impl_->stopped_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// run_once — 事件循环核心
// ---------------------------------------------------------------------------

namespace {

std::chrono::milliseconds next_timeout(std::chrono::milliseconds requested, const std::vector<TimerOpPtr>& timers) {
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
                                recycle_inbound(op);
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
                            recycle_inbound(op);
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
                                    recycle_inbound(op);
                                    continue;
                                }
                                impl_->iocp_sockets.insert(raw->socket);
                            }

                            OVERLAPPED* ov = &raw->overlapped;
                            // IoOperation 已由 acquire_io 中的 *raw = IoOperation{} 整体清零
                            // 此处不再重复 memset

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
                                // 同步完成：Windows IOCP 也会自动投递完成包，加入 iocp_outstanding 等 GQCS 处理
                                raw->transfer_result = bytes_transferred;
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                            } else if (WSAGetLastError() == WSA_IO_PENDING) {
                                // 异步等待 IOCP 完成
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                            } else {
                                // 同步失败：内核不会投递完成包，必须手动触发
                                raw->error_code = WSAGetLastError();
                                raw->transfer_result = 0;
                                impl_->iocp_outstanding[ov] = std::move(op->io);
                                PostQueuedCompletionStatus(impl_->iocp, 0, 0, ov);
                            }
                        } else {
                            // --- 就绪模式（wait_read / wait_write，select 回退） ---
                            impl_->pending[raw] = std::move(op->io);
                        }
                    }
#endif
                    break;

                case InboundOp::Tag::timer:
                    {
                        auto it = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), op->timer->deadline,
                            [](const TimerOpPtr& t, const std::chrono::steady_clock::time_point& dl) {
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
                recycle_inbound(op);
            }
        }
        // 锁外执行 task_func（协程 resume）
        for (auto& task : pending_tasks) {
            task();
            if (impl_->pending_task_count_.fetch_sub(1, std::memory_order_release) == 1) {
                impl_->drain_cv.notify_all();
            }
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
            std::vector<IoOpPtr> to_resume;
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
            std::vector<IoOpPtr> to_resume;
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
    // Windows: IOCP + WSAPoll 混合等待
    {
        std::vector<IoOpPtr> to_resume;

        // 构建 WSAPOLLFD 数组（锁保护 iter + poll_fds 一致性）
        // 用 WSAPoll 而非 select：select 的 fd_set 上限 FD_SETSIZE=64，超出即越界写内存
        std::vector<WSAPOLLFD> poll_fds;
        bool has_iocp = false;
        bool has_pending = false;
        {
            std::lock_guard lock(impl_->mutex);
            has_iocp = !impl_->iocp_outstanding.empty();
            poll_fds.reserve(impl_->pending.size());
            for (auto& [raw, op] : impl_->pending) {
                WSAPOLLFD pfd{};
                pfd.fd = op->socket;
                pfd.events = op->event == IoEvent::read ? POLLRDNORM : POLLWRNORM;
                poll_fds.push_back(pfd);
                has_pending = true;
            }
        }

        // --- Step A: IOCP 完成事件 ---
        // 有 IOCP 操作时等待完成；没有待处理操作时也等待（替代 Sleep，使 PQCS 可唤醒）
        if (has_iocp || !has_pending) {
            OVERLAPPED* ov = nullptr;
            ULONG_PTR completion_key = 0;
            DWORD bytes = 0;

            BOOL ok = GetQueuedCompletionStatus(
                impl_->iocp, &bytes, &completion_key, &ov,
                static_cast<DWORD>(timeout.count()));

            if (ov) {
                auto it = impl_->iocp_outstanding.find(ov);
                if (it == impl_->iocp_outstanding.end()) {
                    log::warn_fmt("IOCP: GQCS orphan ov={} bytes={} ok={}", (void*)ov, bytes, ok);
                }
                if (it != impl_->iocp_outstanding.end()) {
                    auto op = std::move(it->second);
                    impl_->iocp_outstanding.erase(it);

                    // 无论是否已取消，都要 resume：cancelled 的操作在 close_socket 后被内核取消，
                    // 其完成包必须由这里取出才能安全回收 OVERLAPPED（否则复用会触发 use-after-free）。
                    if (op->cancelled) {
                        log::debug_fmt("IOCP: GQCS {} cancelled fd={} bytes={} outstanding={}",
                            op->event == IoEvent::read ? "recv" : "send",
                            static_cast<int>(op->socket), bytes, impl_->iocp_outstanding.size());
                    } else {
                        const char* op_type = op->event == IoEvent::read ? "recv" : "send";
                        if (ok) {
                            op->transfer_result = bytes;
                            if (bytes == 0 && op->event == IoEvent::read) {
                                log::debug_fmt("IOCP: GQCS {} 0-bytes (EOF/FIN) fd={} outstanding={}",
                                    op_type, static_cast<int>(op->socket), impl_->iocp_outstanding.size());
                            } else {
                                log::debug_fmt("IOCP: GQCS {} ok fd={} bytes={} outstanding={}",
                                    op_type, static_cast<int>(op->socket), bytes, impl_->iocp_outstanding.size());
                            }
                        } else if (op->error_code == 0) {
                            DWORD err = GetLastError();
                            if (err == ERROR_SUCCESS) {
                                op->transfer_result = bytes;
                                log::debug_fmt("IOCP: GQCS {} ok=FALSE but ERROR_SUCCESS bytes={} fd={} outstanding={}",
                                    op_type, bytes, static_cast<int>(op->socket), impl_->iocp_outstanding.size());
                            } else {
                                op->error_code = err;
                                op->transfer_result = 0;
                                log::debug_fmt("IOCP: GQCS {} error={} fd={} outstanding={}",
                                    op_type, err, static_cast<int>(op->socket), impl_->iocp_outstanding.size());
                            }
                        }
                    }
                    to_resume.push_back(std::move(op));
                }
            }
        }

        // --- Step B: 就绪模式（WSAPoll 回退） ---
        if (has_pending) {
            // GQCS 已等待过，WSAPoll 只做零超时轮询
            const int poll_timeout = has_iocp ? 0 : static_cast<int>(timeout.count());
            const int count = WSAPoll(poll_fds.data(), static_cast<ULONG>(poll_fds.size()), poll_timeout);

            if (count > 0) {
                // 按 socket 汇总 WSAPoll 结果（POLLERR/HUP/NVAL 也视为就绪，让协程观察到错误）
                std::unordered_map<SOCKET, short> revents_map;
                revents_map.reserve(poll_fds.size());
                for (const auto& pfd : poll_fds) {
                    if (pfd.revents != 0) {
                        revents_map[pfd.fd] = pfd.revents;
                    }
                }
                {
                    std::lock_guard lock(impl_->mutex);
                    auto it = impl_->pending.begin();
                    while (it != impl_->pending.end()) {
                        auto& op = it->second;
                        auto rit = revents_map.find(op->socket);
                        bool ready = false;
                        if (rit != revents_map.end()) {
                            const short revents = rit->second;
                            if (op->event == IoEvent::read) {
                                ready = (revents & (POLLRDNORM | POLLRDBAND | POLLIN |
                                                    POLLERR | POLLHUP | POLLNVAL)) != 0;
                            } else {
                                ready = (revents & (POLLWRNORM | POLLWRBAND | POLLOUT |
                                                    POLLERR | POLLHUP | POLLNVAL)) != 0;
                            }
                        }
                        if (ready) {
                            to_resume.push_back(std::move(it->second));
                            it = impl_->pending.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        }
        // 统一恢复所有完成的 I/O 操作
        for (auto& operation : to_resume) {
            operation->continuation.resume();
        }
        // 协程 resume 后可能提交新的 IOCP 操作，立即唤醒以确保下轮 Phase 1 排空
        if (!to_resume.empty()) {
            wakeup();
        }
        impl_->wakeup.drain();
    }
#endif

    // Phase 4: 处理过期定时器
    {
        std::vector<TimerOpPtr> expired_timers;
        {
            std::lock_guard lock(impl_->mutex);
            const auto now = std::chrono::steady_clock::now();
            auto boundary = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), now,
                [](const TimerOpPtr& timer, const std::chrono::steady_clock::time_point& t) {
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
        std::vector<IoOpPtr> to_resume;
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
                // IOCP 操作：只标记取消，保留在 iocp_outstanding 中。
                // close_socket 会让内核投递取消完成包，由 GQCS 取出并 resume；
                // 若在此提前 erase，OVERLAPPED 会被回收到对象池，而完成包仍引用该地址，
                // 复用后会导致旧完成包误配到新操作（use-after-free）。
#if BEN_GEAR_PLATFORM_WINDOWS
                for (auto& [ov, op] : impl_->iocp_outstanding) {
                    if (op->socket == it->second) {
                        op->cancelled = true;
                    }
                }
#endif
            }
            impl_->close_timeouts.erase(impl_->close_timeouts.begin(), boundary);
        }
        // 锁外关闭 fd：先清 iocp_sockets / close_timeouts 关联，再关闭 fd，
        // 避免 fd 号被新 socket 复用后跳过 CreateIoCompletionPort 关联（Bug 5）
        for (auto fd : fds_to_close) {
            on_socket_closed(fd);
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
    // 标记 loop 线程运行中，供 drain() 协调退出，避免并发 run_once
    impl_->loop_running_.store(true, std::memory_order_release);
    while (!impl_->stopped_.load(std::memory_order_acquire)) {
        run_once();
    }
    impl_->loop_running_.store(false, std::memory_order_release);
    impl_->loop_exit_cv.notify_all();
}

}  // namespace ben_gear::net
