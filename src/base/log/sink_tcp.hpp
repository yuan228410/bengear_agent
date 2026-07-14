#pragma once

#include "base/log/sink.hpp"
#include "base/net/socket.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ben_gear::net {

/// TCP 日志接收器 — 监听端口，向连接的客户端广播日志
/// 由应用层（CLI/Server）负责创建，生命周期与 Logger 一致
class LogTcpSink final : public log::Sink {
public:
    explicit LogTcpSink(std::string host, int port)
        : host_(std::move(host)), port_(port) {
        try {
            listen_fd_ = tcp_listen(host_.c_str(), port_);
            running_.store(true);
            accept_thread_ = std::thread([this] { accept_loop(); });
        } catch (const std::exception& e) {
            std::fprintf(stderr, "LogTcpSink: listen failed on %s:%d - %s (network logging disabled)\n",
                         host_.c_str(), port_, e.what());
        }
    }

    ~LogTcpSink() override {
        running_.store(false);
        listen_fd_.reset();
        if (accept_thread_.joinable()) accept_thread_.join();
    }

    void write(const log::Record&, std::string_view formatted) override {
        if (!running_.load()) return;
        broadcast(formatted);
    }

    void flush() override {}

private:
    void accept_loop() {
        while (running_.load()) {
            auto client = tcp_accept(listen_fd_.get());
            if (!client.valid()) break;
            std::lock_guard lock(clients_mutex_);
            clients_.push_back(std::move(client));
        }
    }

    void broadcast(std::string_view message) {
        std::string payload(message);
        payload += '\n';
        std::lock_guard lock(clients_mutex_);
        std::erase_if(clients_, [&](const Socket& client) {
            const auto sent = socket_send(client.get(), payload.data(), payload.size(), 0);
            if (sent <= 0) {
                std::fprintf(stderr, "LogTcpSink: client disconnected, removing\n");
                return true;
            }
            return false;
        });
    }

    std::string host_;
    int port_;
    NetworkRuntime runtime_;
    Socket listen_fd_;
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<Socket> clients_;
    std::atomic<bool> running_{false};
};

}  // namespace ben_gear::net
