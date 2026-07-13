#pragma once

#include "base/container/string.hpp"

#include <utility>

namespace ben_gear::domain {

namespace container = base::container;

enum class AppErrorCategory {
    invalid_argument,
    not_found,
    permission_denied,
    conflict,
    unavailable,
    internal
};

struct AppError {
    AppErrorCategory category = AppErrorCategory::internal;
    container::String code;
    container::String message;
    container::String details_json;

    static AppError invalid_argument(container::String code, container::String message) {
        return {AppErrorCategory::invalid_argument, std::move(code), std::move(message), container::String()};
    }

    static AppError not_found(container::String code, container::String message) {
        return {AppErrorCategory::not_found, std::move(code), std::move(message), container::String()};
    }

    static AppError permission_denied(container::String code, container::String message) {
        return {AppErrorCategory::permission_denied, std::move(code), std::move(message), container::String()};
    }

    static AppError conflict(container::String code, container::String message) {
        return {AppErrorCategory::conflict, std::move(code), std::move(message), container::String()};
    }

    static AppError unavailable(container::String code, container::String message) {
        return {AppErrorCategory::unavailable, std::move(code), std::move(message), container::String()};
    }

    static AppError internal(container::String code, container::String message) {
        return {AppErrorCategory::internal, std::move(code), std::move(message), container::String()};
    }
};

} // namespace ben_gear::domain
