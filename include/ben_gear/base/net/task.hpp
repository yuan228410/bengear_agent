#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace ben_gear::net {

struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        return handle.promise().continuation ? handle.promise().continuation : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

template <typename T>
class Task {
public:
    using value_type = T;

    struct promise_type {
        std::optional<T> value;
        std::exception_ptr error;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        FinalAwaiter final_suspend() noexcept { return {}; }

        template <typename Value>
        void return_value(Value&& result) {
            value.emplace(std::forward<Value>(result));
        }

        void unhandled_exception() noexcept {
            error = std::current_exception();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type handle = {}) noexcept : handle_(handle) {}
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    bool done() const noexcept { return !handle_ || handle_.done(); }
    void resume() const { handle_.resume(); }

    T result() {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
        return std::move(*handle_.promise().value);
    }

    auto operator co_await() && noexcept {
        struct Awaiter {
            Task task;

            bool await_ready() const noexcept { return task.done(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
                task.handle_.promise().continuation = continuation;
                return task.handle_;
            }

            T await_resume() {
                return task.result();
            }
        };
        return Awaiter{std::move(*this)};
    }

private:
    handle_type handle_;
};

template <>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr error;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            error = std::current_exception();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type handle = {}) noexcept : handle_(handle) {}
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    bool done() const noexcept { return !handle_ || handle_.done(); }
    void resume() const { handle_.resume(); }

    void result() {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
    }

    auto operator co_await() && noexcept {
        struct Awaiter {
            Task task;

            bool await_ready() const noexcept { return task.done(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
                task.handle_.promise().continuation = continuation;
                return task.handle_;
            }

            void await_resume() {
                task.result();
            }
        };
        return Awaiter{std::move(*this)};
    }

private:
    handle_type handle_;
};

}  // namespace ben_gear::net
