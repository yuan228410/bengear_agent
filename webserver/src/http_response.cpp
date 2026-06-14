#include "webserver/http_response.hpp"
#include "webserver/logging.hpp"

#include <charconv>
#include <ctime>
#include <sstream>

namespace ws {

// 状态码与描述映射
static const char* status_reason(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default:
            if (code < 200) return "Unknown";
            if (code < 300) return "OK";
            if (code < 400) return "Redirect";
            if (code < 500) return "Client Error";
            return "Server Error";
    }
}

HttpResponse::HttpResponse()
    : status_code_(200)
    , http_version_("HTTP/1.1")
    , should_close_(false)
{
    // 设置通用头部
    set_header("Server", "BenGear/1.0 (macOS)");
    set_header("Date", get_current_gmt_time());
}

void HttpResponse::set_status(int code) {
    status_code_ = code;
}

void HttpResponse::set_header(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

void HttpResponse::set_body(const std::string& body, const std::string& content_type) {
    body_ = body;
    set_header("Content-Type", content_type);
    set_header("Content-Length", std::to_string(body_.size()));
}

void HttpResponse::set_body(std::string&& body, const std::string& content_type) {
    body_ = std::move(body);
    set_header("Content-Type", content_type);
    set_header("Content-Length", std::to_string(body_.size()));
}

std::string HttpResponse::to_string() const {
    std::string response;
    response.reserve(256 + body_.size());

    // 状态行
    response += http_version_;
    response += ' ';
    response += std::to_string(status_code_);
    response += ' ';
    response += status_reason(status_code_);
    response += "\r\n";

    // 头部
    for (const auto& [name, value] : headers_) {
        response += name;
        response += ": ";
        response += value;
        response += "\r\n";
    }

    // Connection 头部
    if (should_close_) {
        response += "Connection: close\r\n";
    } else {
        response += "Connection: keep-alive\r\n";
    }

    // 空行分隔头部和 body
    response += "\r\n";

    // Body
    response += body_;

    return response;
}

void HttpResponse::set_keep_alive(bool keep_alive) {
    should_close_ = !keep_alive;
}

bool HttpResponse::should_close() const {
    return should_close_;
}

void HttpResponse::clear() {
    status_code_ = 200;
    http_version_ = "HTTP/1.1";
    body_.clear();
    headers_.clear();
    should_close_ = false;
    set_header("Server", "BenGear/1.0 (macOS)");
    set_header("Date", get_current_gmt_time());
}

// 静态工厂方法
HttpResponse HttpResponse::ok(const std::string& body, const std::string& content_type) {
    HttpResponse resp;
    resp.set_body(body, content_type);
    return resp;
}

HttpResponse HttpResponse::json(const std::string& json_body) {
    return ok(json_body, "application/json");
}

HttpResponse HttpResponse::html(const std::string& html_body) {
    return ok(html_body, "text/html; charset=utf-8");
}

HttpResponse HttpResponse::text(const std::string& text_body) {
    return ok(text_body, "text/plain; charset=utf-8");
}

HttpResponse HttpResponse::not_found(const std::string& message) {
    HttpResponse resp;
    resp.set_status(404);
    std::string body = message.empty() ? "404 Not Found" : message;
    resp.set_body(body, "text/plain; charset=utf-8");
    return resp;
}

HttpResponse HttpResponse::error(int code, const std::string& message) {
    HttpResponse resp;
    resp.set_status(code);
    std::string body = message.empty() ? status_reason(code) : message;
    resp.set_body(body, "text/plain; charset=utf-8");
    resp.set_keep_alive(false);
    return resp;
}

HttpResponse HttpResponse::redirect(const std::string& location, bool permanent) {
    HttpResponse resp;
    resp.set_status(permanent ? 301 : 302);
    resp.set_header("Location", location);
    return resp;
}

std::string HttpResponse::get_current_gmt_time() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);

    static const char* weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    char buf[128];
    snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
             weekdays[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
             tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);

    return buf;
}

}  // namespace ws
