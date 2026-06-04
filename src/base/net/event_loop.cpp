#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/platform/os.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <MSWSock.h>
#elif BEN_GEAR_PLATFORM_LINUX
#include <sys/epoll.h>
#else
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

struct EventLoop::Impl {
#if BEN_GEAR_PLATFORM_WINDOWS
    HANDLE completion_port = INVALID_HANDLE_VALUE;
    std::vector<std::shared_ptr<IoOperation>> pending_windows;  // Windows FIFO path
#else
    int poller = -1;
    std::unordered_map<IoOperation*, std::shared_ptr<IoOperation>> pending;  // O(1) lookup by raw ptr
#endif
    std::vector<std::shared_ptr<TimerOperation>> timers;
    std::mutex mutex;
};

EventLoop::EventLoop() : impl_(std::make_unique<Impl>()) {
#if BEN_GEAR_PLATFORM_WINDOWS
    // TODO: IOCP 当前实现仅有关联和假完成包，没有发起真正的 overlapped I/O。
    // 完整实现需要：将 OVERLAPPED 嵌入 IoOperation、通过 WSARecv/WSASend 发起操作、
    // 在 GetQueuedCompletionStatus 返回后通过 overlapped 反查到 IoOperation。
    // 当前可作为 select-based fallback 使用。
    impl_->completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (impl_->completion_port == nullptr) {
        throw std::runtime_error("CreateIoCompletionPort failed");
    }
#elif BEN_GEAR_PLATFORM_LINUX
    impl_->poller = epoll_create1(EPOLL_CLOEXEC);
    if (impl_->poller < 0) {
        throw std::runtime_error("epoll_create1 failed");
    }
#else
    impl_->poller = kqueue();
    if (impl_->poller < 0) {
        throw std::runtime_error("kqueue failed");
    }
#endif
}

EventLoop::~EventLoop() {
#if BEN_GEAR_PLATFORM_WINDOWS
    if (impl_->completion_port != nullptr && impl_->completion_port != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->completion_port);
    }
#elif BEN_GEAR_PLATFORM_POSIX
    if (impl_->poller >= 0) {
        ::close(impl_->poller);
    }
#endif
}

void EventLoop::submit(std::shared_ptr<IoOperation> operation) {
    std::lock_guard lock(impl_->mutex);
#if BEN_GEAR_PLATFORM_WINDOWS
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(operation->socket), impl_->completion_port, 0, 0);
    PostQueuedCompletionStatus(impl_->completion_port, 0, 0, nullptr);
    impl_->pending_windows.push_back(std::move(operation));
#elif BEN_GEAR_PLATFORM_LINUX
    epoll_event event{};
    event.events = EPOLLONESHOT | (operation->event == IoEvent::read ? EPOLLIN : EPOLLOUT);
    auto* raw = operation.get();
    event.data.ptr = raw;
    if (epoll_ctl(impl_->poller, EPOLL_CTL_ADD, raw->socket, &event) < 0) {
        if (errno != EEXIST || epoll_ctl(impl_->poller, EPOLL_CTL_MOD, raw->socket, &event) < 0) {
            throw std::runtime_error("epoll_ctl failed");
        }
    }
    impl_->pending[raw] = std::move(operation);
#else
    struct kevent change{};
    auto* raw = operation.get();
    EV_SET(&change,
           raw->socket,
           raw->event == IoEvent::read ? EVFILT_READ : EVFILT_WRITE,
           EV_ADD | EV_ENABLE | EV_ONESHOT,
           0,
           0,
           raw);
    if (kevent(impl_->poller, &change, 1, nullptr, 0, nullptr) < 0) {
        throw std::runtime_error("kevent register failed");
    }
    impl_->pending[raw] = std::move(operation);
#endif
}

void EventLoop::submit(std::shared_ptr<TimerOperation> operation) {
    std::lock_guard lock(impl_->mutex);
    impl_->timers.push_back(std::move(operation));
}

namespace {

std::chrono::milliseconds next_timeout(std::chrono::milliseconds requested, const std::vector<std::shared_ptr<TimerOperation>>& timers) {
    if (timers.empty()) {
        return requested;
    }
    const auto now = std::chrono::steady_clock::now();
    auto timeout = requested;
    for (const auto& timer : timers) {
        if (timer->deadline <= now) {
            return std::chrono::milliseconds{0};
        }
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(timer->deadline - now);
        timeout = std::min(timeout, std::max(std::chrono::milliseconds{1}, wait));
    }
    return timeout;
}

}  // namespace

void EventLoop::run_once(std::chrono::milliseconds timeout) {
    // Compute timeout under lock, then release for the actual poll
    {
        std::lock_guard lock(impl_->mutex);
        timeout = next_timeout(timeout, impl_->timers);
    }

#if BEN_GEAR_PLATFORM_WINDOWS
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED overlapped = nullptr;
    GetQueuedCompletionStatus(impl_->completion_port, &bytes, &key, &overlapped, static_cast<DWORD>(timeout.count()));
    {
        std::shared_ptr<IoOperation> operation;
        {
            std::lock_guard lock(impl_->mutex);
            if (!impl_->pending_windows.empty()) {
                operation = impl_->pending_windows.front();
                impl_->pending_windows.erase(impl_->pending_windows.begin());
            }
        }
        if (operation) {
            operation->continuation.resume();
        }
    }
#elif BEN_GEAR_PLATFORM_LINUX
    epoll_event events[64]{};
    const int count = epoll_wait(impl_->poller, events, 64, static_cast<int>(timeout.count()));
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
#else
    struct kevent events[64]{};
    timespec time{};
    time.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    time.tv_nsec = static_cast<long>((timeout.count() % 1000) * 1000000);
    const int count = kevent(impl_->poller, nullptr, 0, events, 64, &time);
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
#endif

    // Process expired timers
    std::vector<std::shared_ptr<TimerOperation>> expired_timers;
    {
        std::lock_guard lock(impl_->mutex);
        const auto now = std::chrono::steady_clock::now();
        auto timer = impl_->timers.begin();
        while (timer != impl_->timers.end()) {
            if ((*timer)->deadline > now) {
                ++timer;
                continue;
            }
            expired_timers.push_back(std::move(*timer));
            timer = impl_->timers.erase(timer);
        }
    }
    // Resume timers outside lock
    for (auto& operation : expired_timers) {
        operation->continuation.resume();
    }
}

void EventLoop::run() {
    bool has_work;
    {
        std::lock_guard lock(impl_->mutex);
#if BEN_GEAR_PLATFORM_WINDOWS
        has_work = !impl_->pending_windows.empty() || !impl_->timers.empty();
#else
        has_work = !impl_->pending.empty() || !impl_->timers.empty();
#endif
    }
    while (has_work) {
        run_once();
        std::lock_guard lock(impl_->mutex);
#if BEN_GEAR_PLATFORM_WINDOWS
        has_work = !impl_->pending_windows.empty() || !impl_->timers.empty();
#else
        has_work = !impl_->pending.empty() || !impl_->timers.empty();
#endif
    }
}

}  // namespace ben_gear::net
