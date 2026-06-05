#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/platform/os.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

#if BEN_GEAR_PLATFORM_LINUX
#include <sys/epoll.h>
#elif !BEN_GEAR_PLATFORM_WINDOWS
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace ben_gear::net {

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
// MPSC inbound queue helpers — lock-free push (from coroutines), bulk drain
// (from run_once).  Uses the IntrusiveStack pattern: each producer does an
// atomic compare_exchange to push onto a singly-linked list; the consumer
// swaps the head to nullptr and walks the list in reverse.
// ---------------------------------------------------------------------------

struct EventLoop::Impl {
#if !BEN_GEAR_PLATFORM_WINDOWS
    int poller = -1;
#endif
    std::unordered_map<IoOperation*, std::shared_ptr<IoOperation>> pending;
    std::vector<std::shared_ptr<TimerOperation>> timers;  // sorted by deadline
    // Close-after-timeout entries, sorted by deadline (earliest first).
    // Each entry: {deadline, fd}.  On expiry the fd is closed.
    std::vector<std::pair<std::chrono::steady_clock::time_point, socket_handle>> close_timeouts;
    std::mutex mutex;

    // MPSC lock-free inbound queue
    std::atomic<InboundOp*> inbound_head{nullptr};

    /// Push an InboundOp onto the queue (lock-free, called from any thread)
    void enqueue(InboundOp* op) {
        op->next = inbound_head.load(std::memory_order_relaxed);
        while (!inbound_head.compare_exchange_weak(
                   op->next, op,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {
            // op->next is updated by CAS failure
        }
    }

    /// Drain all pending InboundOps (call from EventLoop thread only)
    /// Returns the list in FIFO order (oldest first).
    std::vector<InboundOp*> drain_inbound() {
        auto* head = inbound_head.exchange(nullptr, std::memory_order_acquire);
        if (!head) return {};

        // Reverse the LIFO list to get FIFO order
        std::vector<InboundOp*> ops;
        for (auto* cur = head; cur; cur = cur->next) {
            ops.push_back(cur);
        }
        std::reverse(ops.begin(), ops.end());
        return ops;
    }
};

EventLoop::EventLoop() : impl_(std::make_unique<Impl>()) {
#if BEN_GEAR_PLATFORM_LINUX
    impl_->poller = epoll_create1(EPOLL_CLOEXEC);
    if (impl_->poller < 0) {
        throw std::runtime_error("epoll_create1 failed");
    }
#elif !BEN_GEAR_PLATFORM_WINDOWS
    impl_->poller = kqueue();
    if (impl_->poller < 0) {
        throw std::runtime_error("kqueue failed");
    }
#endif
}

EventLoop::~EventLoop() {
    // Drain and delete any remaining inbound ops
    auto ops = impl_->drain_inbound();
    for (auto* op : ops) {
        delete op;
    }
#if BEN_GEAR_PLATFORM_POSIX
    if (impl_->poller >= 0) {
        ::close(impl_->poller);
    }
#endif
}

// ---------------------------------------------------------------------------
// submit() — lock-free, pushes into the MPSC inbound queue
// ---------------------------------------------------------------------------

void EventLoop::submit(std::shared_ptr<IoOperation> operation) {
    auto* op = new InboundOp{InboundOp::Tag::io, std::move(operation), nullptr, nullptr};
    impl_->enqueue(op);
}

void EventLoop::submit(std::shared_ptr<TimerOperation> operation) {
    auto* op = new InboundOp{InboundOp::Tag::timer, nullptr, std::move(operation), nullptr};
    impl_->enqueue(op);
}

void EventLoop::close_after(socket_handle fd, std::chrono::milliseconds delay) {
    std::lock_guard lock(impl_->mutex);
    auto deadline = std::chrono::steady_clock::now() + delay;
    auto it = std::lower_bound(impl_->close_timeouts.begin(), impl_->close_timeouts.end(), deadline,
        [](const auto& entry, const auto& dl) { return entry.first < dl; });
    impl_->close_timeouts.insert(it, {deadline, fd});
}

void EventLoop::cancel_close(socket_handle fd) {
    std::lock_guard lock(impl_->mutex);
    // Linear scan by fd — cancel is infrequent (only on success), O(n) is acceptable
    for (auto it = impl_->close_timeouts.begin(); it != impl_->close_timeouts.end(); ++it) {
        if (it->second == fd) {
            impl_->close_timeouts.erase(it);
            return;
        }
    }
}

namespace {

std::chrono::milliseconds next_timeout(std::chrono::milliseconds requested, const std::vector<std::shared_ptr<TimerOperation>>& timers) {
    if (timers.empty()) {
        return requested;
    }
    // Timers are sorted by deadline; earliest is first
    const auto now = std::chrono::steady_clock::now();
    if (timers.front()->deadline <= now) {
        return std::chrono::milliseconds{0};
    }
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(timers.front()->deadline - now);
    return std::min(requested, std::max(std::chrono::milliseconds{1}, wait));
}

}  // namespace

void EventLoop::run_once(std::chrono::milliseconds timeout) {
    // Phase 1: Drain inbound queue into main containers under a single lock.
    // After this, no more inbound items until next run_once.
    {
        auto inbound = impl_->drain_inbound();
        if (!inbound.empty()) {
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
                    // Windows: store in pending map; readiness checked via select() below
                    {
                        auto* raw = op->io.get();
                        impl_->pending[raw] = std::move(op->io);
                    }
#endif
                    break;
                case InboundOp::Tag::timer:
                    {
                        auto it = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), op->timer,
                            [](const std::shared_ptr<TimerOperation>& a, const std::shared_ptr<TimerOperation>& b) {
                                return a->deadline < b->deadline;
                            });
                        impl_->timers.insert(it, std::move(op->timer));
                    }
                    break;
                }
                delete op;
            }
        }
    }

    // Phase 2: Compute timeout (considers both timers and close_timeouts)
    {
        std::lock_guard lock(impl_->mutex);
        timeout = next_timeout(timeout, impl_->timers);
        if (!impl_->close_timeouts.empty()) {
            const auto now = std::chrono::steady_clock::now();
            if (impl_->close_timeouts.front().first <= now) {
                timeout = std::chrono::milliseconds{0};
            } else {
                const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                    impl_->close_timeouts.front().first - now);
                timeout = std::min(timeout, std::max(std::chrono::milliseconds{1}, wait));
            }
        }
    }

    // Phase 3: Poll for I/O (no lock held)
#if BEN_GEAR_PLATFORM_WINDOWS
    {
        // Build fd_sets from pending operations under lock, then call select() without lock.
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        {
            std::lock_guard lock(impl_->mutex);
            for (const auto& [raw, op] : impl_->pending) {
                if (op->event == IoEvent::read) {
                    FD_SET(op->socket, &read_fds);
                } else {
                    FD_SET(op->socket, &write_fds);
                }
            }
        }

        if (impl_->pending.empty()) {
            // No sockets to watch — just sleep for the timeout
            std::this_thread::sleep_for(timeout);
        } else {
            struct timeval tv;
            tv.tv_sec = static_cast<long>(timeout.count() / 1000);
            tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
            // On Windows, select() ignores the first parameter (nfds)
            const int count = select(0, &read_fds, &write_fds, nullptr, &tv);

            // Collect ready operations
            std::vector<std::shared_ptr<IoOperation>> to_resume;
            if (count > 0) {
                std::lock_guard lock(impl_->mutex);
                for (auto it = impl_->pending.begin(); it != impl_->pending.end(); ) {
                    bool ready = false;
                    if (it->second->event == IoEvent::read) {
                        ready = FD_ISSET(it->second->socket, &read_fds);
                    } else {
                        ready = FD_ISSET(it->second->socket, &write_fds);
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
    }
#elif BEN_GEAR_PLATFORM_LINUX
    epoll_event events[64]{};
    const int count = epoll_wait(impl_->poller, events, 64, static_cast<int>(timeout.count()));
    {
        // Swap out ready operations under lock, resume outside
        std::vector<std::shared_ptr<IoOperation>> to_resume;
        {
            std::lock_guard lock(impl_->mutex);
            for (int index = 0; index < count; ++index) {
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
#else
    struct kevent events[64]{};
    timespec time{};
    time.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    time.tv_nsec = static_cast<long>((timeout.count() % 1000) * 1000000);
    const int count = kevent(impl_->poller, nullptr, 0, events, 64, &time);
    {
        // Swap out ready operations under lock, resume outside
        std::vector<std::shared_ptr<IoOperation>> to_resume;
        {
            std::lock_guard lock(impl_->mutex);
            for (int index = 0; index < count; ++index) {
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
#endif

    // Phase 4: Process expired timers (sorted — expired ones form a prefix)
    // Swap out under lock, resume outside
    {
        std::vector<std::shared_ptr<TimerOperation>> expired_timers;
        {
            std::lock_guard lock(impl_->mutex);
            const auto now = std::chrono::steady_clock::now();
            auto boundary = std::lower_bound(impl_->timers.begin(), impl_->timers.end(), now,
                [](const std::shared_ptr<TimerOperation>& timer, const std::chrono::steady_clock::time_point& t) {
                    return timer->deadline <= t;
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

    // Phase 5: Close expired fd timeouts (sorted — expired ones form a prefix)
    {
        std::vector<std::shared_ptr<IoOperation>> to_resume;
        {
            std::lock_guard lock(impl_->mutex);
            const auto now = std::chrono::steady_clock::now();
            auto boundary = std::lower_bound(impl_->close_timeouts.begin(), impl_->close_timeouts.end(), now,
                [](const auto& entry, const auto& t) { return entry.first <= t; });
            for (auto it = impl_->close_timeouts.begin(); it != boundary; ++it) {
                close_socket(it->second);
                // 关闭 fd 后，查找并唤醒挂起在该 fd 上的 I/O 协程
                for (auto pit = impl_->pending.begin(); pit != impl_->pending.end(); ) {
                    if (pit->first->socket == it->second) {
                        pit->second->cancelled = true; // await_resume 时抛 ResponseTimeoutError
                        to_resume.push_back(std::move(pit->second));
                        pit = impl_->pending.erase(pit);
                    } else {
                        ++pit;
                    }
                }
            }
            impl_->close_timeouts.erase(impl_->close_timeouts.begin(), boundary);
        }
        // 锁外恢复协程，避免死锁（协程可能调 cancel_close 获取 mutex）
        for (auto& op : to_resume) {
            op->continuation.resume();
        }
    }
}

void EventLoop::run() {
    bool has_work;
    {
        std::lock_guard lock(impl_->mutex);
        has_work = !impl_->pending.empty() || !impl_->timers.empty() || !impl_->close_timeouts.empty();
    }
    // Also consider pending inbound ops
    if (!has_work) {
        has_work = impl_->inbound_head.load(std::memory_order_acquire) != nullptr;
    }
    while (has_work) {
        run_once();
        {
            std::lock_guard lock(impl_->mutex);
            has_work = !impl_->pending.empty() || !impl_->timers.empty() || !impl_->close_timeouts.empty();
        }
        if (!has_work) {
            has_work = impl_->inbound_head.load(std::memory_order_acquire) != nullptr;
        }
    }
}

}  // namespace ben_gear::net
