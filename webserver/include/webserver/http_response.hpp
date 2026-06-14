#pragma once

#include "webserver/core.hpp"
#include "webserver/http_parser.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <sstream>

namespace ws {

/// HTTP 响应构建器
class HttpResponse {
public:
    HttpResponse() = default;

    /// 设置状态码
    HttpResponse& status(int code) noexcept { status_code_ = code; return *this; }

    /// 设置响应头
    HttpResponse& header(std::string_view key, std::string_view value);

    /// 设置响应体（Content-Type 自动设为 text/html 或其他）
    HttpResponse& body(std::string_view content, std::string_view content_type = "text/plain");

    /// 设置 JSON 响应
    HttpResponse& json(std::string_view json_str);

    /// 设置 404
    HttpResponse& not_found(std::string_view msg = "Not Found");

    /// 设置 500
    HttpResponse& server_error(std::string_view msg = "Internal Server Error");

    /// 构建完整 HTTP 响应字符串
    [[nodiscard]] std::string build() const;

    /// 快速构建错误响应（静态方法）
    static HttpResponse error(int code, std::string_view msg);

private:
    int status_code_ = 200;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::string content_type_ = "text/plain";

    /// 状态码转状态文本
    [[nodiscard]] static std::string_view status_text(int code);
};

}  // namespace ws
