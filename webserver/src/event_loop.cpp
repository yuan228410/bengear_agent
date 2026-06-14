#include "webserver/event_loop.hpp"
#include "webserver/logging.hpp"

#include <sys/event.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <utility>

namespace ws {

// ============ EventLoop ============

EventLoop::EventLoop(size_t max_events)
    : kq_(kqueue()), max_events_(max_events), events_(max_events), running_(false) {

    if (kq_ == -1) {
        log::error_fmt("Failed to create kqueue: {}", std::strerror(errno));
        throw std::runtime_error("kqueue creation failed");
    }

    log::info_fmt("EventLoop created with kqueue fd={}, max_events={}", kq_, max_events);
}

EventLoop::~EventLoop() {
    stop();
    if (kq_ >= 0) {
        ::close(kq_);
        log::info_fmt("EventLoop destroyed, kqueue fd={} closed", kq_);
    }
}

bool EventLoop::add_fd(int fd, uint32_t flags, EventCallback callback) {
    struct kevent ev;

    if (flags & EventFlag::READ) {
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) == -1) {
            log::error_fmt("Failed to add read event for fd={}: {}", fd, std::strerror(errno));
            return false;
        }
    }

    if (flags & EventFlag::WRITE) {
        EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) == -1) {
            log::error_fmt("Failed to add write event for fd={}: {}", fd, std::strerror(errno));
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_[fd] = std::move(callback);
    }

    log::debug_fmt("FD added: fd={}, flags={}", fd, flags);
    return true;
}

bool EventLoop::remove_fd(int fd, uint32_t flags) {
    struct kevent ev;

    if (flags & EventFlag::READ) {
        EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    }

    if (flags & EventFlag::WRITE) {
        EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    }

    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.erase(fd);
    }

    log::debug_fmt("FD removed: fd={}", fd);
    return true;
}

bool EventLoop::modify_fd(int fd, uint32_t flags) {
    // 先删除旧事件，再添加新事件
    remove_fd(fd, EventFlag::READ | EventFlag::WRITE);
    if (flags != 0) {
        add_fd(fd, flags, nullptr); // 保留原有 callback
    }
    return true;
}

void EventLoop::run() {
    running_ = true;
    log::info_fmt("EventLoop started");

    while (running_) {
        // 默认超时 100ms，以便检查 running_ 标志
        struct timespec timeout = {0, 100 * 1000 * 1000}; // 100ms
        int nfds = kevent(kq_, nullptr, 0,
                          events_.data(), static_cast<int>(max_events_),
                          &timeout);

        if (nfds == -1) {
            if (errno == EINTR) {
                continue;  // 被信号中断，继续
            }
            log::error_fmt("kevent failed: {}", std::strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            const auto& event = events_[i];
            int fd = static_cast<int>(event.ident);
            uint32_t flags = 0;

            if (event.filter == EVFILT_READ) {
                flags |= EventFlag::READ;
            }
            if (event.filter == EVFILT_WRITE) {
                flags |= EventFlag::WRITE;
            }

            // 检查错误
            if (event.flags & EV_ERROR) {
                log::error_fmt("Event error on fd={}: {}", fd,
                               std::strerror(static_cast<int>(event.data)));
                flags |= EventFlag::ERROR;
            }

            if (event.flags & EV_EOF) {
                log::debug_fmt("EOF on fd={}", fd);
                flags |= EventFlag::EOF_;
            }

            // 执行回调
            EventCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end()) {
                    callback = it->second;
                }
            }

            if (callback) {
                callback(fd, flags);
            } else {
                log::warn_fmt("No callback registered for fd={}", fd);
            }
        }
    }

    log::info_fmt("EventLoop stopped");
}

void EventLoop::run_once(int timeout_ms) {
    struct timespec timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000 * 1000};
    int nfds = kevent(kq_, nullptr, 0,
                      events_.data(), static_cast<int>(max_events_),
                      &timeout);

    if (nfds == -1) {
        if (errno != EINTR) {
            log::error_fmt("kevent failed: {}", std::strerror(errno));
        }
        return;
    }

    for (int i = 0; i < nfds; ++i) {
        const auto& event = events_[i];
        int fd = static_cast<int>(event.ident);
        uint32_t flags = 0;

        if (event.filter == EVFILT_READ) flags |= EventFlag::READ;
        if (event.filter == EVFILT_WRITE) flags |= EventFlag::WRITE;
        if (event.flags & EV_ERROR) flags |= EventFlag::ERROR;
        if (event.flags & EV_EOF) flags |= EventFlag::EOF_;

        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            auto it = callbacks_.find(fd);
            if (it != callbacks_.end()) {
                callback = it->second;
            }
        }

        if (callback) {
            callback(fd, flags);
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}

int EventLoop::kq_fd() const {
    return kq_;
}

}  // namespace ws
