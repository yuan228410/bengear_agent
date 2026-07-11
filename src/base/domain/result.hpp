#pragma once

#include "base/domain/errors.hpp"

#include <optional>
#include <utility>

namespace ben_gear::domain {

template <class T>
class AppResult {
public:
    static AppResult success(T value) {
        AppResult result;
        result.ok_ = true;
        result.value_ = std::move(value);
        return result;
    }

    static AppResult failure(AppError error) {
        AppResult result;
        result.ok_ = false;
        result.error_ = std::move(error);
        return result;
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    const T& value() const { return *value_; }
    T& value() { return *value_; }
    T&& take_value() { return std::move(*value_); }

    const AppError& error() const { return error_; }

private:
    bool ok_ = false;
    std::optional<T> value_;
    AppError error_ = AppError::internal(container::String("uninitialized"), container::String("result was not initialized"));
};

template <>
class AppResult<void> {
public:
    static AppResult success() {
        AppResult result;
        result.ok_ = true;
        return result;
    }

    static AppResult failure(AppError error) {
        AppResult result;
        result.ok_ = false;
        result.error_ = std::move(error);
        return result;
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    const AppError& error() const { return error_; }

private:
    bool ok_ = false;
    AppError error_ = AppError::internal(container::String("uninitialized"), container::String("result was not initialized"));
};

} // namespace ben_gear::domain
