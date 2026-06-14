#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <chrono>

namespace ws {

/// 高性能字符串（SSO 优化）
using String = std::string;

/// socket 描述符
using Socket = int;

/// 默认端口
constexpr int DEFAULT_PORT = 8080;

/// 默认最大连接数
constexpr int DEFAULT_MAX_CONNECTIONS = 1024;

/// 默认缓冲区大小
constexpr size_t BUFFER_SIZE = 4096;

/// 无效 socket 值
constexpr Socket INVALID_SOCKET = -1;

/// 日志工具
namespace log {

enum Level { DEBUG, INFO, WARN, ERROR };

void set_level(Level l) noexcept;
void debug(const char* fmt, ...) noexcept;
void info(const char* fmt, ...) noexcept;
void warn(const char* fmt, ...) noexcept;
void error(const char* fmt, ...) noexcept;

}  // namespace log

/// 时间工具
namespace time_utils {

/// 获取当前时间戳（毫秒）
[[nodiscard]] int64_t now_ms() noexcept;

}  // namespace time_utils

}  // namespace ws
