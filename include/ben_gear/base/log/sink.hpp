#pragma once

#include "ben_gear/base/log/level.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/net/socket.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ben_gear::log {

// 使用命名空间别名
namespace container = base::container;

struct Record {
    Level level = Level::info;
    std::chrono::system_clock::time_point timestamp{};
    container::String message;  // 使用高性能字符串
    uint64_t thread_id = 0;     // 系统原生线程 ID（短且有意义）
    container::String trace_id; // 追踪标签：user-workspace-session
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Record& record, std::string_view formatted) = 0;
    virtual void flush() {}
};

class StdoutSink final : public Sink {
public:
    void write(const Record&, std::string_view formatted) override {
        std::lock_guard lock(mutex_);
        std::cout << formatted << '\n';
    }

    void flush() override {
        std::lock_guard lock(mutex_);
        std::cout.flush();
    }

private:
    std::mutex mutex_;
};

class FileSink final : public Sink {
public:
    explicit FileSink(std::filesystem::path path, int flush_interval_ms = 1000, int flush_batch_size = 64,
                      size_t max_file_size = 10 * 1024 * 1024, int max_rotated_files = 5)
        : path_(std::move(path)), flush_interval_ms_(flush_interval_ms), flush_batch_size_(flush_batch_size),
          max_file_size_(max_file_size), max_rotated_files_(max_rotated_files) {
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        file_.open(path_, std::ios::app | std::ios::out);
        last_flush_ = std::chrono::steady_clock::now();
    }

    void write(const Record&, std::string_view formatted) override {
        if (!file_) return;
        std::lock_guard lock(mutex_);
        file_ << formatted << '\n';
        current_size_ += formatted.size() + 1;
        ++unflushed_;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_).count();
        if (unflushed_ >= flush_batch_size_ || elapsed_ms >= flush_interval_ms_) {
            file_.flush();
            unflushed_ = 0;
            last_flush_ = now;
        }
        if (current_size_ >= max_file_size_) {
            rotate();
        }
    }

    void flush() override {
        std::lock_guard lock(mutex_);
        file_.flush();
        unflushed_ = 0;
        last_flush_ = std::chrono::steady_clock::now();
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    void rotate() {
        file_.close();
        // 删除最旧的滚动文件
        if (max_rotated_files_ > 0) {
            auto oldest = rotated_path(max_rotated_files_);
            std::error_code ec;
            std::filesystem::remove(oldest, ec);
        }
        // 从大到小编号依次重命名
        for (int i = max_rotated_files_ - 1; i >= 1; --i) {
            auto src = rotated_path(i);
            auto dst = rotated_path(i + 1);
            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
        }
        // 当前文件 -> .1
        auto first = rotated_path(1);
        std::error_code ec;
        std::filesystem::rename(path_, first, ec);
        // 重新打开新文件
        file_.open(path_, std::ios::out);
        current_size_ = 0;
    }

    std::filesystem::path rotated_path(int index) const {
        auto stem = path_.stem().string();
        auto ext = path_.extension().string();
        auto parent = path_.parent_path();
        return parent / (stem + "." + std::to_string(index) + ext);
    }

    std::filesystem::path path_;
    int flush_interval_ms_;
    int flush_batch_size_;
    size_t max_file_size_;
    int max_rotated_files_;
    size_t current_size_ = 0;
    int unflushed_ = 0;
    std::chrono::steady_clock::time_point last_flush_;
    std::ofstream file_;
    std::mutex mutex_;
};

class TcpServerSink final : public Sink {
public:
    explicit TcpServerSink(std::string host, int port)
        : host_(std::move(host)), port_(port) {
        try {
            listen_fd_ = net::tcp_listen(host_.c_str(), port_);
            running_.store(true);
            accept_thread_ = std::thread([this] { accept_loop(); });
        } catch (const std::exception& e) {
            std::cerr << "TcpServerSink: listen failed on " << host_ << ":" << port_
                      << " - " << e.what() << " (network logging disabled)\n";
        }
    }

    ~TcpServerSink() {
        running_.store(false);
        listen_fd_.reset();
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }

    void write(const Record&, std::string_view formatted) override {
        if (!running_.load()) return;
        broadcast(formatted);
    }

    void flush() override {}

private:
    void accept_loop() {
        while (running_.load()) {
            auto client = net::tcp_accept(listen_fd_.get());
            if (!client.valid()) {
                break;
            }
            std::lock_guard lock(clients_mutex_);
            clients_.push_back(std::move(client));
        }
    }

    void broadcast(std::string_view message) {
        std::string payload(message);
        payload += '\n';
        std::lock_guard lock(clients_mutex_);
        std::erase_if(clients_, [&](const net::Socket& client) {
            const auto sent = net::socket_send(client.get(),
                payload.data(), payload.size(), 0);
            if (sent <= 0) {
                std::cerr << "TcpServerSink: client disconnected, removing\n";
                return true;
            }
            return false;
        });
    }

    std::string host_;
    int port_;
    net::NetworkRuntime runtime_;
    net::Socket listen_fd_;
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<net::Socket> clients_;
    std::atomic<bool> running_{false};
};

using SinkList = std::vector<std::shared_ptr<Sink>>;

}  // namespace ben_gear::log
