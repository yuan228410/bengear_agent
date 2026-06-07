#pragma once

#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/connection_pool.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <openssl/ssl.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::net {

namespace container = base::container;

/// HTTP 响应
struct HttpResponse {
    int status = 0;      ///< HTTP 状态码
    std::string body;    ///< 响应体
    container::Map<container::String, std::string> headers;  ///< 响应头（键为小写）
    bool callback_stopped = false;  ///< 回调主动停止（流式请求中解析器提前结束）
    
    /// 检查响应是否成功
    /// @return true 表示 HTTP 状态码为 2xx
    bool ok() const { return status >= 200 && status < 300; }
};

/// HTTP 客户端（统一版本，内置连接池）
///
/// 特性：
/// - 连接复用：自动管理连接池，提升性能
/// - TLS 支持：支持 HTTPS
/// - 流式响应：支持流式读取
/// - 异步 I/O：基于协程的异步接口
/// - 高性能容器：原生支持 container::String / container::Vector
class HttpClient {
public:
    using BodyChunkHandler = std::function<bool(std::string_view)>;

    /// 构造函数
    explicit HttpClient(ConnectionPoolConfig config = {})
        : pool_(std::make_shared<ConnectionPool>(std::move(config))) {}

    // ── 高性能接口（原生容器，零额外转换）─────────────────────

    /// 异步 POST JSON 请求
    Task<HttpResponse> post_json_async(EventLoop& loop,
                                       container::String url,
                                       container::String body,
                                       container::Vector<container::String> headers) const {
        auto req_headers = append_json_header(std::move(headers));
        co_return co_await request_async(loop, "POST", std::move(url), std::move(body), std::move(req_headers), {});
    }

    /// 异步 POST JSON 流式请求
    Task<HttpResponse> post_json_stream_async(EventLoop& loop,
                                              container::String url,
                                              container::String body,
                                              container::Vector<container::String> headers,
                                              BodyChunkHandler on_chunk) const {
        auto stream_headers = append_json_header(std::move(headers));
        stream_headers.push_back(container::String("Accept: text/event-stream"));
        co_return co_await request_async(loop, "POST", std::move(url), std::move(body), std::move(stream_headers), std::move(on_chunk));
    }

    /// 异步 GET 请求
    Task<HttpResponse> get_async(EventLoop& loop, std::string url, std::vector<std::string> headers) const {
        co_return co_await request_async(loop,
            "GET",
            container::String(std::move(url)),
            container::String(),
            to_container_headers(std::move(headers)), {});
    }

    /// POST JSON 流式请求（std 兼容）
    /// 获取连接池
    std::shared_ptr<ConnectionPool> pool() const noexcept {
        return pool_;
    }

private:
    struct ParsedUrl {
        std::string scheme;
        std::string host;
        std::string port;
        std::string target;
        bool tls = false;
    };

    class TlsRuntime {
    public:
        TlsRuntime() {
            std::call_once(init_flag_, [] {
                SSL_library_init();
                SSL_load_error_strings();
                OpenSSL_add_ssl_algorithms();
            });
        }

    private:
        static std::once_flag init_flag_;
    };

    class Transport {
    public:
        Transport(EventLoop& loop, TcpStream stream, bool tls, std::string host)
            : loop_(&loop), stream_(std::move(stream)), tls_(tls), host_(std::move(host)), shutdown_on_cleanup_(false) {}

        Transport(Transport&& other) noexcept
            : loop_(other.loop_),
              stream_(std::move(other.stream_)),
              tls_(other.tls_),
              host_(std::move(other.host_)),
              ctx_(std::exchange(other.ctx_, nullptr)),
              ssl_(std::exchange(other.ssl_, nullptr)),
              shutdown_on_cleanup_(std::exchange(other.shutdown_on_cleanup_, false)) {}

        Transport& operator=(Transport&& other) noexcept {
            if (this != &other) {
                cleanup();
                loop_ = other.loop_;
                stream_ = std::move(other.stream_);
                tls_ = other.tls_;
                host_ = std::move(other.host_);
                ctx_ = std::exchange(other.ctx_, nullptr);
                ssl_ = std::exchange(other.ssl_, nullptr);
                shutdown_on_cleanup_ = std::exchange(other.shutdown_on_cleanup_, false);
            }
            return *this;
        }

        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        ~Transport() {
            cleanup();
        }

        /// 创建新连接（TCP + 可选 TLS）
        static Task<Transport> connect(EventLoop& loop, const ParsedUrl& url) {
            auto stream = co_await async_connect(loop, url.host, url.port);
            Transport transport(loop, std::move(stream), url.tls, url.host);
            if (url.tls) {
                co_await transport.handshake();
            }
            co_return std::move(transport);
        }

        /// 从池中获取的流构造（tls_state 非 null 表示已有 SSL 状态，直接复用）
        static Task<Transport> from_pooled_stream(EventLoop& loop, TcpStream stream,
                                                   bool tls, std::string host,
                                                   void* tls_state = nullptr) {
            Transport transport(loop, std::move(stream), tls, std::move(host));
            if (tls_state) {
                // 池中已有 SSL 对象，fd 不变，直接复用（不能 SSL_clear，否则状态丢失需重新握手）
                transport.ssl_ = static_cast<SSL*>(tls_state);
            }
            co_return std::move(transport);
        }

        /// 提前停止或协议错误时直接丢弃连接，避免 TLS close_notify 阻塞。
        void discard() noexcept {
            shutdown_on_cleanup_ = false;
            stream_.close();
        }

        /// 获取底层 socket 句柄（用于 close_after 超时管理）
        socket_handle native_handle() const noexcept { return stream_.native_handle(); }

        /// 取出底层流和 SSL 状态（归还连接池时使用），Transport 不再拥有资源
        std::pair<TcpStream, void*> detach_stream() {
            // 不执行 SSL_shutdown，保留连接活跃状态供复用
            void* tls = nullptr;
            if (ssl_) {
                tls = ssl_;
                ssl_ = nullptr;
            }
            if (ctx_) {
                SSL_CTX_free(ctx_);
                ctx_ = nullptr;
            }
            return {std::move(stream_), tls};
        }

        Task<void> write_all(std::string_view data) {
            if (!tls_) {
                co_await stream_.write_all(data);
                co_return;
            }
            std::size_t written = 0;
            while (written < data.size()) {
                const int result = SSL_write(ssl_, data.data() + written, static_cast<int>(data.size() - written));
                if (result > 0) {
                    written += static_cast<std::size_t>(result);
                    continue;
                }
                co_await wait_ssl("tls write failed", result);
            }
        }

        Task<std::size_t> read_some(char* data, std::size_t size) {
            if (!tls_) {
                co_return co_await stream_.read_some(data, size);
            }
            for (;;) {
                const int result = SSL_read(ssl_, data, static_cast<int>(size));
                if (result > 0) {
                    co_return static_cast<std::size_t>(result);
                }
                const int error = SSL_get_error(ssl_, result);
                if (error == SSL_ERROR_ZERO_RETURN) {
                    co_return 0;
                }
                co_await wait_ssl("tls read failed", result);
            }
        }

        Task<void> handshake() {
            static TlsRuntime runtime;
            ctx_ = SSL_CTX_new(TLS_client_method());
            if (!ctx_) {
                throw std::runtime_error("SSL_CTX_new failed");
            }
            SSL_CTX_set_default_verify_paths(ctx_);
            ssl_ = SSL_new(ctx_);
            if (!ssl_) {
                throw std::runtime_error("SSL_new failed");
            }
            SSL_set_fd(ssl_, static_cast<int>(static_cast<intptr_t>(stream_.native_handle())));
            SSL_set_tlsext_host_name(ssl_, host_.c_str());
            SSL_set1_host(ssl_, host_.c_str());
            SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);
            for (;;) {
                const int result = SSL_connect(ssl_);
                if (result == 1) {
                    co_return;
                }
                co_await wait_ssl("TLS handshake failed", result);
            }
        }

    private:
        Task<void> wait_ssl(std::string message, int result) {
            const int error = SSL_get_error(ssl_, result);
            if (error == SSL_ERROR_WANT_READ) {
                co_await loop_->wait_read(stream_.native_handle());
                co_return;
            }
            if (error == SSL_ERROR_WANT_WRITE) {
                co_await loop_->wait_write(stream_.native_handle());
                co_return;
            }
            throw std::runtime_error(std::move(message));
        }

        void cleanup() noexcept {
            if (ssl_) {
                if (shutdown_on_cleanup_) {
                    SSL_shutdown(ssl_);
                }
                SSL_free(ssl_);
                ssl_ = nullptr;
            }
            if (ctx_) {
                SSL_CTX_free(ctx_);
                ctx_ = nullptr;
            }
        }

        EventLoop* loop_ = nullptr;
        TcpStream stream_;
        bool tls_ = false;
        std::string host_;
        SSL_CTX* ctx_ = nullptr;
        SSL* ssl_ = nullptr;
        bool shutdown_on_cleanup_ = true;
    };

    // ── 核心请求实现（使用连接池）────────────────────────────

    struct ReadResponseResult {
        HttpResponse response;
        bool body_complete = false;
        bool connection_reusable = false;
    };

    enum class BodyReadStatus {
        complete,
        callback_stopped,
        protocol_error,
    };

    Task<HttpResponse> request_async(EventLoop& loop,
                                      std::string_view method,
                                      container::String url,
                                      container::String body,
                                      container::Vector<container::String> headers,
                                      BodyChunkHandler on_body_chunk) const {
        const auto parsed = parse_url(std::string_view(url.c_str(), url.size()));
        const bool keep_alive = pool_ && pool_->config().enable_keep_alive;
        const auto request_str = build_request(method, parsed, std::string_view(body.c_str(), body.size()),
                                               headers, keep_alive);

        auto result = co_await request_once(loop, parsed, request_str, on_body_chunk, true);
        co_return std::move(result.response);
    }

    Task<ReadResponseResult> request_once(EventLoop& loop,
                                          const ParsedUrl& parsed,
                                          const std::string& request_str,
                                          const BodyChunkHandler& on_body_chunk,
                                          bool allow_reuse_retry) const {
        const bool may_reuse = pool_ && pool_->size(parsed.tls, parsed.host, parsed.port) > 0;
        auto [raw_stream, tls_state] = co_await pool_->acquire(loop, parsed.tls, parsed.host, parsed.port);
        const bool reused = may_reuse || tls_state != nullptr;

        bool retry_fresh = false;
        try {
            auto transport = co_await Transport::from_pooled_stream(loop, std::move(raw_stream), parsed.tls, parsed.host, tls_state);
            if (parsed.tls && tls_state == nullptr) {
                co_await transport.handshake();
            }
            co_return co_await send_with_transport(transport, parsed, request_str, on_body_chunk, tls_state != nullptr, &loop);
        } catch (const std::exception& e) {
            // 超时异常不应重试，直接传播
            if (dynamic_cast<const net::ResponseTimeoutError*>(&e)) {
                throw;
            }
            if (!allow_reuse_retry || !reused) {
                throw;
            }
            log::warn_fmt("http: pooled connection failed: {}, retrying with fresh connection", e.what());
            retry_fresh = true;
        }
        if (retry_fresh) {
            co_return co_await request_fresh(loop, parsed, request_str, on_body_chunk);
        }
        co_return ReadResponseResult{};
    }

    Task<ReadResponseResult> request_fresh(EventLoop& loop,
                                           const ParsedUrl& parsed,
                                           const std::string& request_str,
                                           const BodyChunkHandler& on_body_chunk) const {
        const std::string host_port = parsed.host + ":" + parsed.port;
        log::info_fmt("http: fresh connection to {}", host_port);
        auto transport = co_await Transport::connect(loop, parsed);
        co_return co_await send_with_transport(transport, parsed, request_str, on_body_chunk, false, &loop);
    }

    Task<ReadResponseResult> send_with_transport(Transport& transport,
                                                 const ParsedUrl& parsed,
                                                 const std::string& request_str,
                                                 const BodyChunkHandler& on_body_chunk,
                                                 bool reused_tls,
                                                 EventLoop* loop = nullptr) const {
        const bool keep_alive = pool_ && pool_->config().enable_keep_alive;
        const std::string host_port = parsed.host + ":" + parsed.port;
        log::info_fmt("http: sending request host={} tls={} reused_tls={}", host_port, parsed.tls, reused_tls);
        // 读空闲超时：close_after 在超时时关闭 fd 并唤醒挂起协程
        // 每次成功读到数据后刷新超时，避免 LLM 流式长响应被误杀
        const auto response_timeout = pool_ ? pool_->config().response_timeout : std::chrono::seconds{120};
        const auto raw_fd = transport.native_handle();
        const bool has_timeout = loop && response_timeout.count() > 0;
        // 刷新超时回调：每次读到数据后重新设置 close_after
        std::function<void()> refresh_timeout;
        if (has_timeout) {
            auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(response_timeout);
            loop->close_after(raw_fd, timeout_ms);
            refresh_timeout = [loop, raw_fd, timeout_ms]() {
                loop->cancel_close(raw_fd);
                loop->close_after(raw_fd, timeout_ms);
            };
        }
        ReadResponseResult result;
        try {
            co_await transport.write_all(request_str);
            result = co_await read_response(transport, on_body_chunk, refresh_timeout);
        } catch (...) {
            if (has_timeout) { loop->cancel_close(raw_fd); }
            throw;
        }
        if (has_timeout) { loop->cancel_close(raw_fd); }
        log::info_fmt("http: response status={} complete={} reusable={}",
                      result.response.status, result.body_complete, result.connection_reusable);
        // 复用连接返回 status=0（服务端已关闭），抛异常触发上层重试新连接
        if (result.response.status == 0 && reused_tls) {
            transport.discard();
            throw std::runtime_error("pooled TLS connection closed by server (eof before headers)");
        }
        if (keep_alive && result.body_complete && result.connection_reusable && result.response.status > 0) {
            auto [stream, tls] = transport.detach_stream();
            pool_->release(parsed.tls, parsed.host, parsed.port, std::move(stream), tls);
            log::info_fmt("http: connection returned to pool host={}", host_port);
        } else {
            transport.discard();
        }
        co_return result;
    }

    // ── 辅助方法 ────────────────────────────────────────────────

    static container::Vector<container::String> append_json_header(container::Vector<container::String> headers) {
        headers.push_back(container::String("Content-Type: application/json"));
        return headers;
    }

    static container::Vector<container::String> to_container_headers(const std::vector<std::string>& headers) {
        container::Vector<container::String> result;
        for (const auto& h : headers) {
            result.push_back(container::String(h.c_str()));
        }
        return result;
    }

    static container::Vector<container::String> to_container_headers(std::vector<std::string>&& headers) {
        container::Vector<container::String> result;
        for (auto& h : headers) {
            result.push_back(container::String(std::move(h).c_str()));
        }
        return result;
    }

    static ParsedUrl parse_url(std::string_view url) {
        const auto scheme_end = url.find("://");
        if (scheme_end == std::string_view::npos) {
            throw std::runtime_error("url missing scheme");
        }
        ParsedUrl parsed;
        parsed.scheme = base::utils::to_lower(std::string(url.substr(0, scheme_end)));
        parsed.tls = parsed.scheme == "https";
        if (parsed.scheme != "http" && parsed.scheme != "https") {
            throw std::runtime_error("unsupported url scheme: " + parsed.scheme);
        }
        auto authority_begin = scheme_end + 3;
        auto path_begin = url.find('/', authority_begin);
        auto authority = path_begin == std::string_view::npos ? url.substr(authority_begin) : url.substr(authority_begin, path_begin - authority_begin);
        parsed.target = path_begin == std::string_view::npos ? "/" : std::string(url.substr(path_begin));
        auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            parsed.host = std::string(authority.substr(0, colon));
            parsed.port = std::string(authority.substr(colon + 1));
        } else {
            parsed.host = std::string(authority);
            parsed.port = parsed.tls ? "443" : "80";
        }
        if (parsed.host.empty()) {
            throw std::runtime_error("url missing host");
        }
        return parsed;
    }

    static std::string build_request(std::string_view method,
                                     const ParsedUrl& url,
                                     std::string_view body,
                                     const container::Vector<container::String>& headers,
                                     bool keep_alive = false) {
        static constexpr std::string_view http_version = " HTTP/1.1\r\nHost: ";
        static constexpr std::string_view fixed_headers = "\r\nUser-Agent: BenGear/0.1\r\nAccept: */*\r\nConnection: ";
        static constexpr std::string_view keep_alive_val = "keep-alive\r\n";
        static constexpr std::string_view close_val = "close\r\n";
        static constexpr std::string_view content_length_hdr = "Content-Length: ";
        static constexpr std::string_view header_end = "\r\n\r\n";

        // 预计算总长度，避免多次 realloc
        size_t total_size = 512 + body.size();  // 基础大小
        
        // 添加自定义 headers 大小
        for (const auto& header : headers) {
            total_size += header.size() + 2;  // +2 for "\r\n"
        }
        
        // 添加 Content-Length 数字大小
        if (!body.empty()) {
            // 预估数字长度（最多 20 位）
            total_size += content_length_hdr.size() + 20;
        }

        std::string request;
        request.reserve(total_size);

        // Request line + Host
        request.append(method);
        request += ' ';
        request.append(url.target);
        request.append(http_version);
        request.append(url.host);

        // Fixed headers + Connection
        request.append(fixed_headers);
        request.append(keep_alive ? keep_alive_val : close_val);

        // Custom headers
        for (const auto& header : headers) {
            request.append(header.c_str(), header.size());
            request.append("\r\n");
        }

        // Content-Length
        if (!body.empty()) {
            request.append(content_length_hdr);
            request.append(std::to_string(body.size()));
        }

        // Blank line + body
        request.append(header_end);
        request.append(body);
        return request;
    }

    static Task<ReadResponseResult> read_response(Transport& transport, const BodyChunkHandler& on_body_chunk, const std::function<void()>& refresh_timeout) {
        ReadResponseResult result;
        std::string buffer;
        buffer.reserve(8192);        std::array<char, 8192> chunk{};
        std::size_t header_end = std::string::npos;
        while (header_end == std::string::npos) {
            const auto size = co_await transport.read_some(chunk.data(), chunk.size());
            if (size == 0) {
                log::warn_fmt("http: eof before headers complete, buf_len={}", buffer.size());
                result.response = HttpResponse{0, std::move(buffer), {}};
                co_return result;
            }
            buffer.append(chunk.data(), size);
            if (refresh_timeout) { refresh_timeout(); }
            header_end = buffer.find("\r\n\r\n");
        }

        // Parse headers from string_view — zero-copy view into buffer
        const std::string_view header_block(buffer.data(), header_end);
        auto headers = parse_headers(header_block);
        result.response.status = parse_status(header_block);

        // Extract needed values before moving headers into response
        const auto transfer_encoding = header_value(headers, "transfer-encoding");
        const auto content_length = header_value(headers, "content-length");
        const auto connection = header_value(headers, "connection");
        const bool server_allows_reuse = connection.find("close") == std::string::npos;
        result.response.headers = std::move(headers);

        // Trim header from buffer in-place — body data stays at offset 0
        buffer.erase(0, header_end + 4);

        log::info_fmt("http: response status={} transfer-encoding={} content-length={} connection={} initial_body_len={}",
                      result.response.status, transfer_encoding, content_length, connection, buffer.size());

        if (transfer_encoding.find("chunked") != std::string::npos) {
            const auto body_status = co_await read_chunked_body(transport, std::move(buffer), result.response.body, on_body_chunk, refresh_timeout);
            result.body_complete = body_status != BodyReadStatus::protocol_error;
            result.response.callback_stopped = (body_status == BodyReadStatus::callback_stopped);
            // callback_stopped 时需 drain 剩余 body 才能复用连接
            if (body_status == BodyReadStatus::callback_stopped && server_allows_reuse) {
                const auto drain_status = co_await drain_chunked_body(transport, refresh_timeout);
                result.connection_reusable = (drain_status == BodyReadStatus::complete);
            } else {
                result.connection_reusable = result.body_complete && server_allows_reuse &&
                                             body_status == BodyReadStatus::complete;
            }
            co_return result;
        }

        if (!content_length.empty()) {
            std::size_t total_len = 0;
            try {
                total_len = static_cast<std::size_t>(std::stoull(content_length));
            } catch (const std::exception& e) {
                log::error_fmt("http: invalid content-length '{}': {}", content_length, e.what());
                result.connection_reusable = false;
                co_return result;
            }

            // Move initial body data from buffer directly into response body
            const auto initial_len = std::min(buffer.size(), total_len);
            if (initial_len > 0) {
                result.response.body = std::move(buffer);
                result.response.body.resize(initial_len);
                if (on_body_chunk && !on_body_chunk(std::string_view(result.response.body.data(), initial_len))) {
                    log::info_fmt("http: fixed-length body interrupted by callback");
                    result.response.callback_stopped = true;
                    result.body_complete = false;
                    result.connection_reusable = false;
                    co_return result;
                }
            }

            while (result.response.body.size() < total_len) {
                const auto remaining = total_len - result.response.body.size();
                const auto to_read = std::min(remaining, chunk.size());
                const auto size = co_await transport.read_some(chunk.data(), to_read);
                if (size == 0) {
                    log::warn_fmt("http: eof before fixed body complete, have={} need={}", result.response.body.size(), total_len);
                    co_return result;
                }
                result.response.body.append(chunk.data(), size);
                if (refresh_timeout) { refresh_timeout(); }
                if (on_body_chunk && !on_body_chunk(std::string_view(chunk.data(), size))) {
                    log::info_fmt("http: fixed-length body interrupted by callback");
                    result.response.callback_stopped = true;
                    result.body_complete = false;
                    result.connection_reusable = false;
                    co_return result;
                }
            }

            result.body_complete = true;
            result.connection_reusable = server_allows_reuse;
            co_return result;
        }

        result.response.body = std::move(buffer);
        if (on_body_chunk && !result.response.body.empty() && !on_body_chunk(result.response.body)) {
            log::info_fmt("http: unknown-length body interrupted by callback");
            result.response.callback_stopped = true;
            result.body_complete = false;
            result.connection_reusable = false;
            co_return result;
        }

        if (server_allows_reuse) {
            // 无长度信息的 keep-alive 响应无法确定消息边界，不能继续阻塞等待或复用连接。
            log::warn_fmt("http: response has no body delimiter on reusable connection");
            result.body_complete = true;
            result.connection_reusable = false;
            co_return result;
        }

        for (;;) {
            const auto size = co_await transport.read_some(chunk.data(), chunk.size());
            if (size == 0) {
                result.body_complete = true;
                result.connection_reusable = false;
                co_return result;
            }
            result.response.body.append(chunk.data(), size);
            if (refresh_timeout) { refresh_timeout(); }
            if (on_body_chunk && !on_body_chunk(std::string_view(chunk.data(), size))) {
                log::info_fmt("http: close-delimited body interrupted by callback");
                result.body_complete = false;
                result.connection_reusable = false;
                co_return result;
            }
        }
    }

    static int parse_status(std::string_view header_block) {
        auto first_space = header_block.find(' ');
        if (first_space == std::string_view::npos) {
            return 0;
        }
        auto second_space = header_block.find(' ', first_space + 1);
        const auto status = header_block.substr(first_space + 1, second_space - first_space - 1);
        return std::atoi(std::string(status).c_str());
    }

    static container::Map<container::String, std::string> parse_headers(std::string_view header_block) {
        container::Map<container::String, std::string> headers;
        std::size_t begin = 0;
        for (;;) {
            auto end = header_block.find("\r\n", begin);
            auto line = end == std::string_view::npos ? header_block.substr(begin) : header_block.substr(begin, end - begin);
            auto colon = line.find(':');
            if (colon != std::string_view::npos) {
                auto key_sv = line.substr(0, colon);
                auto value_sv = line.substr(colon + 1);
                // Lowercase key in-place to avoid to_lower() temporary
                std::string key_str(key_sv);
                for (auto& c : key_str) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                // Trim key (leading/trailing whitespace)
                auto key_start = key_str.find_first_not_of(" \t");
                auto key_end = key_str.find_last_not_of(" \t");
                if (key_start != std::string::npos) {
                    key_str = key_str.substr(key_start, key_end - key_start + 1);
                }
                // Trim value
                while (!value_sv.empty() && (value_sv.front() == ' ' || value_sv.front() == '\t')) {
                    value_sv.remove_prefix(1);
                }
                while (!value_sv.empty() && (value_sv.back() == ' ' || value_sv.back() == '\t')) {
                    value_sv.remove_suffix(1);
                }
                headers[container::String(key_str.c_str())] = std::string(value_sv);
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 2;
        }
        return headers;
    }

    static std::string header_value(const container::Map<container::String, std::string>& headers, std::string_view key) {
        if (auto it = headers.find(key); it != headers.end()) {
            return base::utils::to_lower(it->second);
        }
        return {};
    }

    static Task<bool> ensure_buffer(Transport& transport, std::string& buffer,
                                    std::size_t pos, std::size_t needed,
                                    const std::function<void()>& refresh_timeout = {}) {
        std::array<char, 8192> read_buffer{};
        while (buffer.size() - pos < needed) {
            const auto to_read = std::min(read_buffer.size(), needed - (buffer.size() - pos));
            const auto read = co_await transport.read_some(read_buffer.data(), to_read);
            if (read == 0) {
                log::warn_fmt("http: eof while filling buffer, have={} need={}", buffer.size() - pos, needed);
                co_return false;
            }
            buffer.append(read_buffer.data(), read);
            if (refresh_timeout) { refresh_timeout(); }
        }
        co_return true;
    }

    static Task<bool> ensure_line(Transport& transport, std::string& buffer, std::size_t pos,
                                   const std::function<void()>& refresh_timeout = {}) {
        std::array<char, 8192> read_buffer{};
        while (buffer.find("\r\n", pos) == std::string::npos) {
            const auto read = co_await transport.read_some(read_buffer.data(), read_buffer.size());
            if (read == 0) {
                log::warn_fmt("http: eof while reading line, buf_len={}", buffer.size() - pos);
                co_return false;
            }
            buffer.append(read_buffer.data(), read);
            if (refresh_timeout) { refresh_timeout(); }
        }
        co_return true;
    }

    static bool parse_chunk_size(std::string_view line, std::size_t& size) noexcept {
        if (auto extension = line.find(';'); extension != std::string_view::npos) {
            line = line.substr(0, extension);
        }
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
            line.remove_prefix(1);
        }
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            return false;
        }

        std::size_t value = 0;
        for (const char ch : line) {
            unsigned digit = 0;
            if (ch >= '0' && ch <= '9') {
                digit = static_cast<unsigned>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                digit = static_cast<unsigned>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                digit = static_cast<unsigned>(ch - 'A' + 10);
            } else {
                return false;
            }
            if (value > (static_cast<std::size_t>(-1) - digit) / 16) {
                return false;
            }
            value = value * 16 + digit;
        }
        size = value;
        return true;
    }

    /// drain 剩余 chunked body，使连接可复用（callback_stopped 后调用）
    static Task<BodyReadStatus> drain_chunked_body(Transport& transport,
                                                   const std::function<void()>& refresh_timeout) {
        std::string buffer;
        std::size_t pos = 0;
        for (;;) {
            if (!(co_await ensure_line(transport, buffer, pos, refresh_timeout))) {
                co_return BodyReadStatus::protocol_error;
            }
            const auto line_end = buffer.find("\r\n", pos);
            std::size_t chunk_size = 0;
            if (!parse_chunk_size(std::string_view(buffer.data() + pos, line_end - pos), chunk_size)) {
                co_return BodyReadStatus::protocol_error;
            }
            pos = line_end + 2;
            if (chunk_size == 0) {
                if (!(co_await consume_chunked_trailers(transport, buffer, pos, refresh_timeout))) {
                    co_return BodyReadStatus::protocol_error;
                }
                co_return BodyReadStatus::complete;
            }
            const auto frame_size = chunk_size + 2;
            if (!(co_await ensure_buffer(transport, buffer, pos, frame_size, refresh_timeout))) {
                co_return BodyReadStatus::protocol_error;
            }
            pos += frame_size;
            if (pos > 4096) { buffer.erase(0, pos); pos = 0; }
        }
    }

    static Task<BodyReadStatus> read_chunked_body(Transport& transport,
                                                  std::string buffer,
                                                  std::string& output,
                                                  const BodyChunkHandler& on_body_chunk,
                                                  const std::function<void()>& refresh_timeout = {}) {
        output.reserve(output.size() + buffer.size());
        std::size_t pos = 0;

        // Compact: discard consumed prefix when it exceeds threshold
        auto compact = [&] {
            if (pos > 4096) {
                buffer.erase(0, pos);
                pos = 0;
            }
        };

        for (;;) {
            compact();
            if (!(co_await ensure_line(transport, buffer, pos, refresh_timeout))) {
                log::warn_fmt("http: chunked body ended before size line, output_len={}", output.size());
                co_return BodyReadStatus::protocol_error;
            }

            const auto line_end = buffer.find("\r\n", pos);
            std::size_t chunk_size = 0;
            if (!parse_chunk_size(std::string_view(buffer.data() + pos, line_end - pos), chunk_size)) {
                log::warn_fmt("http: invalid chunk size line '{}'", buffer.substr(pos, line_end - pos));
                co_return BodyReadStatus::protocol_error;
            }
            pos = line_end + 2;

            if (chunk_size == 0) {
                if (!(co_await consume_chunked_trailers(transport, buffer, pos, refresh_timeout))) {
                    co_return BodyReadStatus::protocol_error;
                }
                co_return BodyReadStatus::complete;
            }

            const auto frame_size = chunk_size + 2;
            if (frame_size < chunk_size || !(co_await ensure_buffer(transport, buffer, pos, frame_size, refresh_timeout))) {
                log::warn_fmt("http: chunked body ended in chunk, chunk_size={} output_len={}", chunk_size, output.size());
                co_return BodyReadStatus::protocol_error;
            }
            if (buffer[pos + chunk_size] != '\r' || buffer[pos + chunk_size + 1] != '\n') {
                log::warn_fmt("http: chunk missing CRLF, chunk_size={}", chunk_size);
                co_return BodyReadStatus::protocol_error;
            }

            const std::string_view chunk_view(buffer.data() + pos, chunk_size);
            output.append(chunk_view);
            pos += frame_size;
            const bool should_continue = !on_body_chunk || on_body_chunk(chunk_view);
            if (!should_continue) {
                log::info_fmt("http: chunked body callback stopped, output_len={}", output.size());
                co_return BodyReadStatus::callback_stopped;
            }
        }
    }

    static Task<bool> consume_chunked_trailers(Transport& transport, std::string& buffer, std::size_t& pos,
                                                const std::function<void()>& refresh_timeout) {
        for (;;) {
            if (!(co_await ensure_line(transport, buffer, pos, refresh_timeout))) {
                log::warn_fmt("http: chunked body missing trailer terminator");
                co_return false;
            }
            const auto line_end = buffer.find("\r\n", pos);
            if (line_end == pos) {
                pos += 2;
                co_return true;
            }
            pos = line_end + 2;
        }
    }

    std::shared_ptr<ConnectionPool> pool_;
};

// Static member definition for TlsRuntime once_flag
inline std::once_flag HttpClient::TlsRuntime::init_flag_;

}  // namespace ben_gear::net

namespace ben_gear {
using HttpClient = net::HttpClient;
using HttpResponse = net::HttpResponse;
}  // namespace ben_gear
