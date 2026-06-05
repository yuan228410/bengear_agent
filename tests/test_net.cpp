#include <gtest/gtest.h>
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/http.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/retry.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#endif

namespace {

#ifndef _WIN32
class TestHttpServer {
public:
    explicit TestHttpServer(std::string response, int max_requests = 1)
        : response_(std::move(response)), max_requests_(max_requests) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("socket failed");
        }
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            throw std::runtime_error("bind failed");
        }
        if (::listen(listen_fd_, 4) != 0) {
            throw std::runtime_error("listen failed");
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            throw std::runtime_error("getsockname failed");
        }
        port_ = ntohs(addr.sin_port);
        worker_ = std::thread([this] { run(); });
    }

    ~TestHttpServer() {
        if (listen_fd_ >= 0) ::close(listen_fd_);
        if (worker_.joinable()) worker_.join();
    }

    int port() const noexcept { return port_; }
    int requests() const noexcept { return requests_.load(); }
    std::string last_request() const {
        std::lock_guard lock(request_mutex_);
        return last_request_;
    }

private:
    void run() {
        for (int i = 0; i < max_requests_; ++i) {
            const int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) return;
            requests_.fetch_add(1);
            char buffer[1024];
            const auto received = ::recv(client, buffer, sizeof(buffer), 0);
            if (received > 0) {
                std::lock_guard lock(request_mutex_);
                last_request_.assign(buffer, static_cast<std::size_t>(received));
            }
            std::size_t written = 0;
            while (written < response_.size()) {
                const auto sent = ::send(client,
                    response_.data() + written,
                    response_.size() - written,
                    0);
                if (sent <= 0) {
                    break;
                }
                written += static_cast<std::size_t>(sent);
            }
            ::close(client);
        }
    }

    std::string response_;
    int max_requests_ = 1;
    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<int> requests_{0};
    mutable std::mutex request_mutex_;
    std::string last_request_;
    std::thread worker_;
};
#endif

ben_gear::net::Task<int> immediate_task() {
    co_return 42;
}

ben_gear::net::Task<int> timer_task(ben_gear::net::EventLoop& loop) {
    co_await loop.sleep_for(std::chrono::milliseconds{1});
    co_return 7;
}

ben_gear::net::Task<ben_gear::ChatResult> retry_async_task(
        ben_gear::net::EventLoop& /*loop*/, int& attempts) {
    ben_gear::config::Settings settings;
    settings.llm_request_retry.max_attempts = 3;
    for (int attempt = 1; attempt <= settings.llm_request_retry.max_attempts; ++attempt) {
        ++attempts;
        if (attempts >= 3) {
            co_return ben_gear::ChatResult{200, "async-ok", "raw", {}};
        }
    }
    co_return ben_gear::ChatResult{503, "failed", "raw", {}};
}

}  // namespace

TEST(CoroutineTask, StartsSuspended) {
    auto task = immediate_task();
    EXPECT_FALSE(task.done());
}

TEST(CoroutineTask, CompletesAfterResume) {
    auto task = immediate_task();
    task.resume();
    EXPECT_TRUE(task.done());
    EXPECT_EQ(task.result(), 42);
}

TEST(EventLoop, TimerAwaiter) {
    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    loop.run_once(std::chrono::milliseconds{0});
    EXPECT_EQ(loop.run(timer_task(loop)), 7);
}

TEST(EventLoop, AsyncRetry) {
    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    int attempts = 0;
    const auto result = loop.run(retry_async_task(loop, attempts));
    EXPECT_EQ(attempts, 3);
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(result.text, "async-ok");
}

#ifndef _WIN32
TEST(HttpClient, ContentLengthKeepAliveReturnsWithoutEof) {
    TestHttpServer server("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\nhello");
    ben_gear::net::HttpClient client;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/";
    auto response = client.post_json(std::string_view(url), std::string_view("{}"), std::vector<std::string>{});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "hello");
    EXPECT_EQ(client.pool()->size("127.0.0.1", std::to_string(server.port())), 1U);
}

TEST(HttpClient, ChunkedStreamReadsDoneMarkerNaturally) {
    const std::string body = "5\r\ndata1\r\nD\r\ndata: [DONE]\n\r\n0\r\n\r\n";
    TestHttpServer server("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n" + body);
    ben_gear::net::HttpClient client;
    int chunks = 0;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/";
    auto response = client.post_json_stream(std::string_view(url), std::string_view("{}"), std::vector<std::string>{},
        [&](std::string_view chunk) {
            ++chunks;
            return chunk.find("[DONE]") == std::string_view::npos;
        });
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(chunks, 2);
    EXPECT_NE(response.body.find("[DONE]"), std::string::npos);
    EXPECT_EQ(client.pool()->size("127.0.0.1", std::to_string(server.port())), 0U);
}

TEST(HttpClient, ConnectionCloseIsNotPooled) {
    TestHttpServer server("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
    ben_gear::net::HttpClient client;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/";
    auto response = client.post_json(std::string_view(url), std::string_view("{}"), std::vector<std::string>{});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "hello");
    EXPECT_EQ(client.pool()->size("127.0.0.1", std::to_string(server.port())), 0U);
}

TEST(HttpClient, GetUsesGetMethod) {
    TestHttpServer server("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
    ben_gear::net::HttpClient client;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/resource";
    auto response = client.get(std::string_view(url), std::vector<std::string>{});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");
    EXPECT_TRUE(server.last_request().starts_with("GET /resource HTTP/1.1\r\n"));
}

TEST(HttpClient, ChunkedWithExtensionAndTrailerCompletes) {
    const std::string body = "5;foo=bar\r\nhello\r\n0\r\nX-Trailer: yes\r\n\r\n";
    TestHttpServer server("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n" + body);
    ben_gear::net::HttpClient client;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/";
    auto response = client.post_json_stream(std::string_view(url), std::string_view("{}"), std::vector<std::string>{},
        [](std::string_view) { return true; });
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "hello");
    EXPECT_EQ(client.pool()->size("127.0.0.1", std::to_string(server.port())), 1U);
}

TEST(HttpClient, FixedLengthCallbackStopDropsConnectionWithoutDrain) {
    TestHttpServer server("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: keep-alive\r\n\r\nhelloworld");
    ben_gear::net::HttpClient client;
    const auto url = std::string("http://127.0.0.1:") + std::to_string(server.port()) + "/";
    auto response = client.post_json_stream(std::string_view(url), std::string_view("{}"), std::vector<std::string>{},
        [](std::string_view) { return false; });
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "helloworld");
    EXPECT_EQ(client.pool()->size("127.0.0.1", std::to_string(server.port())), 0U);
}
#endif
