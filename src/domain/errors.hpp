#pragma once


#include <utility>

namespace ben_gear::domain {

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
    std::string code;
    std::string message;
    std::string details_json;

    static AppError invalid_argument(std::string code, std::string message) {
        return {AppErrorCategory::invalid_argument, std::move(code), std::move(message), std::string()};
    }

    static AppError not_found(std::string code, std::string message) {
        return {AppErrorCategory::not_found, std::move(code), std::move(message), std::string()};
    }

    static AppError permission_denied(std::string code, std::string message) {
        return {AppErrorCategory::permission_denied, std::move(code), std::move(message), std::string()};
    }

    static AppError conflict(std::string code, std::string message) {
        return {AppErrorCategory::conflict, std::move(code), std::move(message), std::string()};
    }

    static AppError unavailable(std::string code, std::string message) {
        return {AppErrorCategory::unavailable, std::move(code), std::move(message), std::string()};
    }

    static AppError internal(std::string code, std::string message) {
        return {AppErrorCategory::internal, std::move(code), std::move(message), std::string()};
    }
};

} // namespace ben_gear::domain
