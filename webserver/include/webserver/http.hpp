#pragma once

#include "webserver/core.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstring>

namespace ws {

/// HTTP 方法
enum class HttpMethod : uint8_t {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    UNKNOWN,
};

/// HTTP 版本
enum class HttpVersion : uint8_t {
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2_0,
    UNKNOWN,
};

/// HTTP 请求
struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;
    std::string query_string;
    HttpVersion version = HttpVersion::HTTP_1_1;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive = true;

    /// 获取某个 header 值，不存在返回空串
    [[nodiscard]] std::string_view get_header(std::string_view key) const {
        auto it = headers.find(std::string(key));
        return it != headers.end() ? std::string_view(it->second) : std::string_view();
    }

    void reset() {
        method = HttpMethod::UNKNOWN;
        path.clear();
        query_string.clear();
        version = HttpVersion::HTTP_1_1;
        headers.clear();
        body.clear();
        keep_alive = true;
    }
};

/// HTTP 响应
struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    /// 设置 Content-Type
    void set_content_type(std::string_view type) {
        headers["Content-Type"] = std::string(type);
    }

    /// 设置响应体并自动计算 Content-Length
    void set_body(std::string_view data) {
        body = std::string(data);
        headers["Content-Length"] = std::to_string(body.size());
    }

    /// 序列化为 HTTP 响应字符串
    [[nodiscard]] std::string to_string() const {
        std::string resp;
        resp.reserve(256 + body.size());

        // 状态行
        resp.append("HTTP/1.1 ")
            .append(std::to_string(status_code))
            .append(" ")
            .append(status_text)
            .append("\r\n");

        // 响应头
        for (const auto& [key, value] : headers) {
            resp.append(key).append(": ").append(value).append("\r\n");
        }

        resp.append("\r\n");
        resp.append(body);

        return resp;
    }

    /// 创建简单的文本响应
    static HttpResponse text(int code, std::string_view text, bool keep_alive = true) {
        HttpResponse resp;
        resp.status_code = code;
        resp.status_text = (code == 200) ? "OK" : 
                          (code == 404) ? "Not Found" :
                          (code == 500) ? "Internal Server Error" : "Unknown";
        resp.set_content_type("text/plain; charset=utf-8");
        resp.set_body(text);
        if (!keep_alive) {
            resp.headers["Connection"] = "close";
        }
        return resp;
    }

    /// 创建 HTML 响应
    static HttpResponse html(int code, std::string_view html_content, bool keep_alive = true) {
        HttpResponse resp;
        resp.status_code = code;
        resp.status_text = (code == 200) ? "OK" : "Not Found";
        resp.set_content_type("text/html; charset=utf-8");
        resp.set_body(html_content);
        if (!keep_alive) {
            resp.headers["Connection"] = "close";
        }
        return resp;
    }

    /// 创建 JSON 响应
    static HttpResponse json(int code, std::string_view json_body, bool keep_alive = true) {
        HttpResponse resp;
        resp.status_code = code;
        resp.status_text = (code == 200) ? "OK" : "Bad Request";
        resp.set_content_type("application/json");
        resp.set_body(json_body);
        if (!keep_alive) {
            resp.headers["Connection"] = "close";
        }
        return resp;
    }

    /// 404 页面
    static HttpResponse not_found(bool keep_alive = true) {
        return text(404, "404 Not Found", keep_alive);
    }

    /// 500 页面
    static HttpResponse server_error(bool keep_alive = true) {
        return text(500, "500 Internal Server Error", keep_alive);
    }
};

}  // namespace ws
