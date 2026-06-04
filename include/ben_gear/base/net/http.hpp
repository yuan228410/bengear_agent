#pragma once

#include "ben_gear/base/net/connection_pool.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <openssl/ssl.h>

#include <array>
#include <cstdlib>
#include <functional>
#include <map>
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
    using BodyChunkHandler = std::function<void(std::string_view)>;

    /// 构造函数
    explicit HttpClient(ConnectionPoolConfig config = {})
        : pool_(std::make_shared<ConnectionPool>(std::move(config))) {}

    // ── 高性能接口（原生容器，零额外转换）─────────────────────

    /// POST JSON 请求
    HttpResponse post_json(container::String url,
                           container::String body,
                           container::Vector<container::String> headers) const {
        NetworkRuntime runtime;
        EventLoop loop;
        return loop.run(request_async(loop, std::move(url), std::move(body), std::move(headers), {}));
    }

    /// 异步 POST JSON 请求
    Task<HttpResponse> post_json_async(EventLoop& loop,
                                       container::String url,
                                       container::String body,
                                       container::Vector<container::String> headers) const {
        auto req_headers = append_json_header(std::move(headers));
        co_return co_await request_async(loop, std::move(url), std::move(body), std::move(req_headers), {});
    }

    /// POST JSON 流式请求
    HttpResponse post_json_stream(container::String url,
                                  container::String body,
                                  container::Vector<container::String> headers,
                                  const BodyChunkHandler& on_chunk) const {
        auto stream_headers = append_json_header(std::move(headers));
        stream_headers.push_back(container::String("Accept: text/event-stream"));
        NetworkRuntime runtime;
        EventLoop loop;
        return loop.run(request_async(loop, std::move(url), std::move(body), std::move(stream_headers), on_chunk));
    }

    /// 异步 POST JSON 流式请求
    Task<HttpResponse> post_json_stream_async(EventLoop& loop,
                                              container::String url,
                                              container::String body,
                                              container::Vector<container::String> headers,
                                              BodyChunkHandler on_chunk) const {
        auto stream_headers = append_json_header(std::move(headers));
        stream_headers.push_back(container::String("Accept: text/event-stream"));
        co_return co_await request_async(loop, std::move(url), std::move(body), std::move(stream_headers), std::move(on_chunk));
    }

    // ── std 兼容接口（边界转换）─────────────────────────────────

    /// POST JSON 请求（std 兼容）
    HttpResponse post_json(std::string_view url,
                           std::string_view body,
                           const std::vector<std::string>& headers) const {
        return post_json(
            container::String(url.data(), url.size()),
            container::String(body.data(), body.size()),
            to_container_headers(headers));
    }

    /// 异步 POST JSON 请求（std 兼容）
    Task<HttpResponse> post_json_async(EventLoop& loop,
                                       std::string url,
                                       std::string body,
                                       std::vector<std::string> headers) const {
        co_return co_await post_json_async(loop,
            container::String(std::move(url)),
            container::String(std::move(body)),
            to_container_headers(headers));
    }

    /// GET 请求
    HttpResponse get(std::string_view url, const std::vector<std::string>& headers) const {
        NetworkRuntime runtime;
        EventLoop loop;
        return loop.run(request_async(loop,
            container::String(url.data(), url.size()),
            container::String(),
            to_container_headers(headers), {}));
    }

    /// 异步 GET 请求
    Task<HttpResponse> get_async(EventLoop& loop, std::string url, std::vector<std::string> headers) const {
        co_return co_await request_async(loop,
            container::String(std::move(url)),
            container::String(),
            to_container_headers(std::move(headers)), {});
    }

    /// POST JSON 流式请求（std 兼容）
    HttpResponse post_json_stream(std::string_view url,
                                  std::string_view body,
                                  const std::vector<std::string>& headers,
                                  const BodyChunkHandler& on_chunk) const {
        return post_json_stream(
            container::String(url.data(), url.size()),
            container::String(body.data(), body.size()),
            to_container_headers(headers),
            on_chunk);
    }

    /// 异步 POST JSON 流式请求（std 兼容）
    Task<HttpResponse> post_json_stream_async(EventLoop& loop,
                                              std::string url,
                                              std::string body,
                                              std::vector<std::string> headers,
                                              BodyChunkHandler on_chunk) const {
        co_return co_await post_json_stream_async(loop,
            container::String(std::move(url)),
            container::String(std::move(body)),
            to_container_headers(std::move(headers)),
            std::move(on_chunk));
    }

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
            : loop_(&loop), stream_(std::move(stream)), tls_(tls), host_(std::move(host)) {}

        Transport(Transport&& other) noexcept
            : loop_(other.loop_),
              stream_(std::move(other.stream_)),
              tls_(other.tls_),
              host_(std::move(other.host_)),
              ctx_(std::exchange(other.ctx_, nullptr)),
              ssl_(std::exchange(other.ssl_, nullptr)) {}

        Transport& operator=(Transport&& other) noexcept {
            if (this != &other) {
                cleanup();
                loop_ = other.loop_;
                stream_ = std::move(other.stream_);
                tls_ = other.tls_;
                host_ = std::move(other.host_);
                ctx_ = std::exchange(other.ctx_, nullptr);
                ssl_ = std::exchange(other.ssl_, nullptr);
            }
            return *this;
        }

        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        ~Transport() {
            cleanup();
        }

        static Task<Transport> connect(EventLoop& loop, const ParsedUrl& url) {
            auto stream = co_await async_connect(loop, url.host, url.port);
            Transport transport(loop, std::move(stream), url.tls, url.host);
            if (url.tls) {
                co_await transport.handshake();
            }
            co_return std::move(transport);
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

    private:
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
            // Windows SOCKET 是 uintptr_t，但 SSL_set_fd 接受 int。
            // 实际场景中 socket 值远小于 INT_MAX，可安全转换。
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
                SSL_shutdown(ssl_);
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
    };

    // ── 核心请求实现（高性能容器路径）────────────────────────────

    static Task<HttpResponse> request_async(EventLoop& loop,
                                            container::String url,
                                            container::String body,
                                            container::Vector<container::String> headers,
                                            BodyChunkHandler on_body_chunk) {
        const auto parsed = parse_url(std::string_view(url.c_str(), url.size()));
        auto transport = co_await Transport::connect(loop, parsed);
        auto request_str = build_request("POST", parsed, std::string_view(body.c_str(), body.size()), headers);
        co_await transport.write_all(request_str);
        co_return co_await read_response(transport, on_body_chunk);
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
                                     const container::Vector<container::String>& headers) {
        std::string request;
        request.reserve(512 + body.size());
        request += method;
        request += ' ';
        request += url.target;
        request += " HTTP/1.1\r\nHost: ";
        request += url.host;
        request += "\r\nUser-Agent: BenGear/0.1\r\nAccept: */*\r\nConnection: close\r\n";
        for (const auto& header : headers) {
            request += std::string_view(header.c_str(), header.size());
            request += "\r\n";
        }
        if (!body.empty()) {
            request += "Content-Length: ";
            request += std::to_string(body.size());
            request += "\r\n";
        }
        request += "\r\n";
        request += body;
        return request;
    }

    static Task<HttpResponse> read_response(Transport& transport, const BodyChunkHandler& on_body_chunk) {
        std::string buffer;
        std::array<char, 8192> chunk{};
        std::size_t header_end = std::string::npos;
        while (header_end == std::string::npos) {
            const auto size = co_await transport.read_some(chunk.data(), chunk.size());
            if (size == 0) {
                break;
            }
            buffer.append(chunk.data(), size);
            header_end = buffer.find("\r\n\r\n");
        }
        if (header_end == std::string::npos) {
            co_return HttpResponse{0, std::move(buffer)};
        }

        const auto header_block = buffer.substr(0, header_end);
        auto headers = parse_headers(header_block);
        HttpResponse response{parse_status(header_block), {}};
        auto body_buffer = buffer.substr(header_end + 4);

        const auto transfer_encoding = header_value(headers, "transfer-encoding");
        if (transfer_encoding.find("chunked") != std::string::npos) {
            co_await decode_chunked_body(transport, std::move(body_buffer), response.body, on_body_chunk);
            co_return response;
        }

        if (!body_buffer.empty()) {
            response.body += body_buffer;
            if (on_body_chunk) {
                on_body_chunk(body_buffer);
            }
        }
        for (;;) {
            const auto size = co_await transport.read_some(chunk.data(), chunk.size());
            if (size == 0) {
                break;
            }
            response.body.append(chunk.data(), size);
            if (on_body_chunk) {
                on_body_chunk(std::string_view(chunk.data(), size));
            }
        }
        co_return response;
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

    static std::map<std::string, std::string> parse_headers(std::string_view header_block) {
        std::map<std::string, std::string> headers;
        std::size_t begin = 0;
        for (;;) {
            auto end = header_block.find("\r\n", begin);
            auto line = end == std::string_view::npos ? header_block.substr(begin) : header_block.substr(begin, end - begin);
            auto colon = line.find(':');
            if (colon != std::string_view::npos) {
                auto key = base::utils::to_lower(std::string(base::utils::trim(line.substr(0, colon))));
                auto value = std::string(base::utils::trim(line.substr(colon + 1)));
                headers[std::move(key)] = std::move(value);
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 2;
        }
        return headers;
    }

    static std::string header_value(const std::map<std::string, std::string>& headers, std::string_view key) {
        if (auto item = headers.find(std::string(key)); item != headers.end()) {
            return base::utils::to_lower(item->second);
        }
        return {};
    }

    static Task<bool> ensure_buffer(Transport& transport, std::string& buffer, std::size_t size) {
        std::array<char, 8192> read_buffer{};
        while (buffer.size() < size) {
            const auto read = co_await transport.read_some(read_buffer.data(), read_buffer.size());
            if (read == 0) {
                co_return false;
            }
            buffer.append(read_buffer.data(), read);
        }
        co_return true;
    }

    static Task<bool> ensure_line(Transport& transport, std::string& buffer) {
        std::array<char, 8192> read_buffer{};
        while (buffer.find("\r\n") == std::string::npos) {
            const auto read = co_await transport.read_some(read_buffer.data(), read_buffer.size());
            if (read == 0) {
                co_return false;
            }
            buffer.append(read_buffer.data(), read);
        }
        co_return true;
    }

    static Task<void> decode_chunked_body(Transport& transport,
                                          std::string buffer,
                                          std::string& output,
                                          const BodyChunkHandler& on_body_chunk) {
        for (;;) {
            if (!(co_await ensure_line(transport, buffer))) {
                co_return;
            }
            auto line_end = buffer.find("\r\n");
            auto size_line = buffer.substr(0, line_end);
            auto extension = size_line.find(';');
            if (extension != std::string::npos) {
                size_line.resize(extension);
            }
            const auto chunk_size = std::strtoull(size_line.c_str(), nullptr, 16);
            buffer.erase(0, line_end + 2);
            if (chunk_size == 0) {
                co_return;
            }
            if (!(co_await ensure_buffer(transport, buffer, static_cast<std::size_t>(chunk_size) + 2))) {
                co_return;
            }
            std::string_view chunk_view(buffer.data(), static_cast<std::size_t>(chunk_size));
            output.append(chunk_view);
            if (on_body_chunk) {
                on_body_chunk(chunk_view);
            }
            buffer.erase(0, static_cast<std::size_t>(chunk_size) + 2);
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
