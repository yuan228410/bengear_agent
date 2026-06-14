#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <deque>
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
    bool alive_ = true;

    // 写队列：保证 WS 帧顺序，避免并发写导致帧交错
    std::deque<std::string> write_queue_;
    // 紧急队列：控制帧（pong）走此队列，flush_writes 在每帧间隙优先发送
    std::deque<std::string> urgent_queue_;
    // ★ 挂起的协议级 pong 帧：由 read_loop 设置，flush_writes 在每轮循环检查发送
    //   避免 read_loop 直接调用 send_pong（write_frame）与 flush_writes 并发写 socket
    std::string pending_pong_;
    bool flushing_ = false;
};

std::string compute_ws_accept(const std::string& ws_key);
bool is_ws_upgrade(const std::string& method, const std::string& path,
                   const std::map<std::string, std::string>& headers);

} // namespace ben_gear::server
