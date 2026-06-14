#include "webserver/response_builder.hpp"
#include "webserver/logging.hpp"

#include <sstream>
#include <ctime>
#include <iomanip>

namespace ws {

// 状态码文本
static const std::unordered_map<int, std::string> status_texts = {
    {200, "OK"},
    {201, "Created"},
    {204, "No Content"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {304, "Not Modified"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {408, "Request Timeout"},
    {413, "Payload Too Large"},
    {429, "Too Many Requests"},
    {500, "Internal Server Error"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
};

std::string status_text(int status_code) {
    auto it = status_texts.find(status_code);
    if (it != status_texts.end()) {
        return it->second;
    }
    return "Unknown";
}

// ============ ResponseBuilder ============

ResponseBuilder& ResponseBuilder::status(int code) {
    status_code_ = code;
    return *this;
}

ResponseBuilder& ResponseBuilder::header(const std::string& key, const std::string& value) {
    headers_[key] = value;
    return *this;
}

ResponseBuilder& ResponseBuilder::body(const std::string& body) {
    body_ = body;
    return *this;
}

ResponseBuilder& ResponseBuilder::content_type(const std::string& type) {
    headers_["Content-Type"] = type;
    return *this;
}

ResponseBuilder& ResponseBuilder::json() {
    headers_["Content-Type"] = "application/json";
    return *this;
}

ResponseBuilder& ResponseBuilder::html() {
    headers_["Content-Type"] = "text/html; charset=utf-8";
    return *this;
}

ResponseBuilder& ResponseBuilder::text() {
    headers_["Content-Type"] = "text/plain; charset=utf-8";
    return *this;
}

std::string ResponseBuilder::build() {
    std::ostringstream oss;

    // 状态行
    oss << "HTTP/1.1 " << status_code_ << " "
        << status_text(status_code_) << "\r\n";

    // Content-Length
    oss << "Content-Length: " << body_.size() << "\r\n";

    // 添加默认 Date header（如果没设置）
    if (headers_.find("Date") == headers_.end()) {
        oss << "Date: " << get_current_time() << "\r\n";
    }

    // Server header
    if (headers_.find("Server") == headers_.end()) {
        oss << "Server: WebServer/1.0\r\n";
    }

    // Connection header（默认 keep-alive）
    if (headers_.find("Connection") == headers_.end()) {
        oss << "Connection: keep-alive\r\n";
    }

    // 自定义 headers
    for (const auto& [key, value] : headers_) {
        if (key != "Content-Type" || headers_.find("Content-Type") != headers_.end()) {
            // 跳过 Content-Type，下面统一处理
        }
        oss << key << ": " << value << "\r\n";
    }

    // Content-Type
    auto ct = headers_.find("Content-Type");
    if (ct != headers_.end()) {
        oss << "Content-Type: " << ct->second << "\r\n";
    } else {
        oss << "Content-Type: text/plain\r\n";
    }

    // 空行分隔 headers 和 body
    oss << "\r\n";

    // Body
    oss << body_;

    return oss.str();
}

// 静态方法：快速创建响应

std::string ResponseBuilder::ok(const std::string& body, const std::string& content_type) {
    return ResponseBuilder()
        .status(200)
        .body(body)
        .content_type(content_type)
        .build();
}

std::string ResponseBuilder::ok_json(const std::string& json_body) {
    return ResponseBuilder()
        .status(200)
        .body(json_body)
        .content_type("application/json")
        .build();
}

std::string ResponseBuilder::ok_html(const std::string& html_body) {
    return ResponseBuilder()
        .status(200)
        .body(html_body)
        .content_type("text/html; charset=utf-8")
        .build();
}

std::string ResponseBuilder::not_found(const std::string& message) {
    return ResponseBuilder()
        .status(404)
        .body(message.empty() ? "404 Not Found" : message)
        .text()
        .build();
}

std::string ResponseBuilder::server_error(const std::string& message) {
    return ResponseBuilder()
        .status(500)
        .body(message.empty() ? "500 Internal Server Error" : message)
        .text()
        .build();
}

std::string ResponseBuilder::redirect(const std::string& location) {
    return ResponseBuilder()
        .status(302)
        .header("Location", location)
        .body("")
        .build();
}

std::string ResponseBuilder::get_current_time() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    gmtime_r(&now, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();
}

}  // namespace ws
