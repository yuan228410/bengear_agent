#pragma once

#include "net/tcp_stream.hpp"
#include "net/task.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <deque>
#include <mutex>
#include <string>

namespace ben_gear::server {

enum class WsOpcode : uint8_t {
    continuation = 0x0, text = 0x1, binary = 0x2,
    close = 0x8, ping = 0x9, pong = 0xA,
};

struct WsFrame {
    WsOpcode opcode = WsOpcode::text;
    bool fin = true;
    std::string payload;
};

class WsHandler {
public:
    using OnMessage = std::function<void(std::string_view)>;
    using OnClose = std::function<void()>;

    WsHandler(net::TcpStream stream, std::string ws_key);

    net::Task<void> handshake(const std::string& origin = {});
    net::Task<void> send_text(std::string_view message);
    net::Task<void> send_binary(std::string_view data);
    net::Task<void> send_close(uint16_t code = 1000, std::string_view reason = {});
    net::Task<void> send_pong(std::string_view payload = {});

    /// 线程安全写入队列：消息入队，单协程顺序 flush，保证帧不交错
    void queue_send(std::string json);
    /// 高优先级写入队列：插入队首（用于 pong 等不能延迟的关键消息）
    void queue_send_front(std::string json);
    /// 紧急写入：走独立 urgent 队列，flush_writes 在每帧间隙优先发送
    /// 确保控制帧（pong）不被大 token 帧阻塞
    void queue_send_urgent(std::string json);

    net::Task<void> read_loop(OnMessage on_message, OnClose on_close);

    net::EventLoop& loop() noexcept { return stream_.loop(); }

    bool alive() const noexcept { return alive_; }
    bool is_flushing() const noexcept { return flushing_; }
    size_t queue_size() const noexcept { return write_queue_.size(); }
    void close();

private:
    net::Task<void> flush_writes();
    net::Task<WsFrame> read_frame();
    net::Task<void> write_frame(WsOpcode opcode, bool fin, std::string_view payload);
    static void apply_mask(uint8_t* data, size_t len, uint32_t mask_key);

    net::TcpStream stream_;
    std::string ws_key_;
    std::atomic<bool> alive_{true};

    // 写队列：保证 WS 帧顺序，避免并发写导致帧交错
    // write_mutex_ 保护 write_queue_ / urgent_queue_ / queued_bytes_ / pending_pong_
    // 的跨线程访问（queue_send* 可从任意线程调用）
    mutable std::mutex write_mutex_;
    std::deque<std::string> write_queue_;
    std::deque<std::string> urgent_queue_;
    size_t queued_bytes_ = 0;
    // ★ 挂起的协议级 pong 帧：由 read_loop 设置，flush_writes 在每轮循环检查发送
    std::string pending_pong_;
    std::atomic<bool> flushing_{false};
    // write_frame 日志时间戳（成员变量替代 static 局部变量，协程中 static 语义不明）
    std::chrono::steady_clock::time_point last_frame_log_{};
};

std::string compute_ws_accept(const std::string& ws_key);
bool is_ws_upgrade(const std::string& method, const std::string& path,
                   const std::map<std::string, std::string>& headers);

} // namespace ben_gear::server
