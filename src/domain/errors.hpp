#pragma once

#include <stdexcept>
#include <string>
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

/// 统一的应用级异常基类，所有领域层错误均应使用此类型或其子类
struct AppError : public std::runtime_error {
    AppErrorCategory category = AppErrorCategory::internal;
    std::string code;
    std::string details_json;

    AppError(AppErrorCategory cat, std::string c, std::string msg)
        : std::runtime_error(msg), category(cat), code(std::move(c)) {}

    static AppError invalid_argument(std::string code, std::string message) {
        return {AppErrorCategory::invalid_argument, std::move(code), std::move(message)};
    }

    static AppError not_found(std::string code, std::string message) {
        return {AppErrorCategory::not_found, std::move(code), std::move(message)};
    }

    static AppError permission_denied(std::string code, std::string message) {
        return {AppErrorCategory::permission_denied, std::move(code), std::move(message)};
    }

    static AppError conflict(std::string code, std::string message) {
        return {AppErrorCategory::conflict, std::move(code), std::move(message)};
    }

    static AppError unavailable(std::string code, std::string message) {
        return {AppErrorCategory::unavailable, std::move(code), std::move(message)};
    }

    static AppError internal(std::string code, std::string message) {
        return {AppErrorCategory::internal, std::move(code), std::move(message)};
    }
};

} // namespace ben_gear::domain
