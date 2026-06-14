#pragma once

#include "webserver/http.hpp"
#include <string>
#include <string_view>
#include <cstring>

namespace ws {

/// HTTP 请求解析器（有限状态机）
class HttpParser {
public:
    enum class State : uint8_t {
        METHOD,        // 解析方法
        PATH,          // 解析路径
        QUERY_STRING,  // 解析查询参数
        VERSION,       // 解析版本
        HEADER_KEY,    // 解析 header 键
        HEADER_VALUE,  // 解析 header 值
        BODY,          // 解析 body
        DONE,          // 解析完成
        ERROR,         // 解析错误
    };

    /// 解析结果
    struct Result {
        bool success = false;
        size_t consumed = 0;  ///< 已消费的字节数
        std::string error_msg;
    };

    HttpParser() = default;

    /// 重置解析器状态
    void reset();

    /// 解析数据
    /// @param data 数据缓冲区
    /// @param len 数据长度
    /// @return 解析结果
    Result parse(const char* data, size_t len);

    /// 获取解析完成的请求
    [[nodiscard]] const HttpRequest& request() const { return request_; }

    /// 是否解析完成
    [[nodiscard]] bool is_done() const { return state_ == State::DONE; }

    /// 是否解析出错
    [[nodiscard]] bool has_error() const { return state_ == State::ERROR; }

private:
    /// 尝试解析 HTTP 方法
    bool parse_method(const char* data, size_t len, size_t& pos);
    
    /// 尝试解析路径和查询参数
    bool parse_path(const char* data, size_t len, size_t& pos);
    
    /// 尝试解析 HTTP 版本
    bool parse_version(const char* data, size_t len, size_t& pos);
    
    /// 尝试解析 header 行
    bool parse_header_line(const char* data, size_t len, size_t& pos);
    
    /// 尝试解析 body
    bool parse_body(const char* data, size_t len, size_t& pos);

    /// 跳过 \r\n
    bool skip_line_ending(const char* data, size_t len, size_t& pos);

    State state_ = State::METHOD;
    HttpRequest request_;
    std::string header_key_;      ///< 临时保存当前解析的 header key
    size_t body_remaining_ = 0;   ///< body 剩余字节数

    /// HTTP 方法名到枚举的映射
    static constexpr std::string_view METHOD_GET    = "GET";
    static constexpr std::string_view METHOD_POST   = "POST";
    static constexpr std::string_view METHOD_PUT    = "PUT";
    static constexpr std::string_view METHOD_DELETE = "DELETE";
    static constexpr std::string_view METHOD_HEAD   = "HEAD";
};

}  // namespace ws
