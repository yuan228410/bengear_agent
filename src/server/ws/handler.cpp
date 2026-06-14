#include "ben_gear/server/ws/handler.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mbedtls/sha1.h>

namespace ben_gear::server {
namespace container = base::container;

static const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string compute_ws_accept(const std::string& ws_key) {
    std::string combined = ws_key + WS_MAGIC;
    unsigned char hash[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);
    static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r; r.reserve(28);
    for(int i=0;i<20;i+=3){
        uint32_t n=static_cast<uint32_t>(hash[i])<<16;
        if(i+1<20) n|=static_cast<uint32_t>(hash[i+1])<<8;
        if(i+2<20) n|=static_cast<uint32_t>(hash[i+2]);
        r.push_back(b64[(n>>18)&0x3F]); r.push_back(b64[(n>>12)&0x3F]);
        r.push_back((i+1<20)?b64[(n>>6)&0x3F]:'=');
        r.push_back((i+2<20)?b64[n&0x3F]:'=');
    }
    return r;
}

bool is_ws_upgrade(const std::string& method, const std::string&,
                   const std::map<std::string, std::string>& headers) {
    if(method!="GET") return false;
    auto it=headers.find("upgrade"); if(it==headers.end()) return false;
    std::string v=it->second; std::transform(v.begin(),v.end(),v.begin(),::tolower);
    return v=="websocket";
}

WsHandler::WsHandler(net::TcpStream stream, std::string ws_key)
    : stream_(std::move(stream)), ws_key_(std::move(ws_key)) {}

net::Task<void> WsHandler::handshake(const std::string& origin) {
    auto accept_key = compute_ws_accept(ws_key_);
    container::String resp;
    resp.append("HTTP/1.1 101 Switching Protocols\r\n");
    resp.append("Upgrade: websocket\r\nConnection: Upgrade\r\n");
    resp.append("Sec-WebSocket-Accept: ");
    resp.append(container::String(accept_key.c_str()));
    resp.append("\r\n");
    if(!origin.empty()){resp.append("Access-Control-Allow-Origin: ");resp.append(container::String(origin.c_str()));resp.append("\r\n");}
    resp.append("\r\n");
    co_await stream_.write_all(std::string_view(resp.data(),resp.size()));
    log::debug_fmt("WS handshake completed");
}

net::Task<void> WsHandler::send_text(std::string_view msg){co_await write_frame(WsOpcode::text,true,msg);}
net::Task<void> WsHandler::send_binary(std::string_view d){co_await write_frame(WsOpcode::binary,true,d);}
net::Task<void> WsHandler::send_pong(std::string_view p){co_await write_frame(WsOpcode::pong,true,p);}

net::Task<void> WsHandler::send_close(uint16_t code,std::string_view reason){
    char p[2]={(char)((code>>8)&0xFF),(char)(code&0xFF)};
    std::string payload(p,2); payload.append(reason.data(),reason.size());
    co_await write_frame(WsOpcode::close,true,payload); alive_=false;
}

net::Task<void> WsHandler::read_loop(OnMessage on_msg, OnClose on_close) {
    try {
        while(alive_){
            auto frame=co_await read_frame();
            if(!alive_) break;
            switch(frame.opcode){
            case WsOpcode::text: case WsOpcode::binary: on_msg(frame.payload); break;
            case WsOpcode::ping:
                // ★ 关键修复：不能直接 send_pong（write_frame + stream_.write_all），
                //   因为 flush_writes 协程也可能同时往 socket 写数据（write_all），
                //   两个协程并发写同一个 socket → TCP 帧交错，浏览器无法正确解析 pong。
                //   改为挂起 pong 数据，让 flush_writes 在安全时机发送。
                log::debug_fmt("WS ping received, pending pong (avoid concurrent write)");
                pending_pong_ = frame.payload;
                // flush_writes 可能处于 idle 状态，需要确保它被唤醒
                if (!flushing_) {
                    flushing_ = true;
                    net::fire_and_forget(stream_.loop(), flush_writes());
                }
                break;
            case WsOpcode::close: alive_=false; break;
            default: break;
            }
        }
    } catch(const std::exception& e){
        log::error_fmt("WS read_loop exception: {}", e.what());
        alive_=false;
        stream_.close();
    }
    alive_=false; if(on_close) on_close();
}

void WsHandler::queue_send(std::string json) {
    if (!alive_) return;
    write_queue_.push_back(std::move(json));
    // 如果没有正在执行的 flush，启动一个
    if (!flushing_) {
        flushing_ = true;
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

void WsHandler::queue_send_front(std::string json) {
    if (!alive_) return;
    write_queue_.push_front(std::move(json));
    if (!flushing_) {
        flushing_ = true;
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

void WsHandler::queue_send_urgent(std::string json) {
    if (!alive_) return;
    urgent_queue_.push_back(std::move(json));
    if (!flushing_) {
        flushing_ = true;
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

net::Task<void> WsHandler::flush_writes() {
    // 单协程顺序 flush 写队列，保证 WS 帧不交错
    // 每发完一帧优先检查 urgent_queue_（控制帧如 pong），确保不被大 token 阻塞
    try {
        auto start_ts = std::chrono::steady_clock::now();
        size_t drain_count = 0;
        while (alive_) {
            // ★ 检查挂起的 ping 级 pong 帧（由 read_loop 设置，非写线程安全）
            if (!pending_pong_.empty()) {
                auto payload = std::move(pending_pong_);
                pending_pong_.clear();
                log::debug_fmt("WS flush send pending pong len={}", payload.size());
                co_await write_frame(WsOpcode::pong, true, payload);
                drain_count++;
            }
            // 紧急队列绝对优先：每帧之间检查，确保控制帧不被阻塞
            while (alive_ && !urgent_queue_.empty()) {
                auto msg = std::move(urgent_queue_.front());
                urgent_queue_.pop_front();
                log::debug_fmt("WS flush urgent msg_len={}", msg.size());
                co_await send_text(msg);
                drain_count++;
            }
            if (write_queue_.empty()) break;
            auto msg = std::move(write_queue_.front());
            write_queue_.pop_front();
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_ts).count();
            log::debug_fmt("WS flush send msg_len={} queue_remaining={} urgent={} elapsed_in_flush={}ms",
                          msg.size(), write_queue_.size(), urgent_queue_.size(), elapsed_ms);
            co_await send_text(msg);
            drain_count++;
        }
        if (drain_count > 0) {
            auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_ts).count();
            log::debug_fmt("WS flush_writes done drained={} total={}ms", drain_count, total_ms);
        }
    } catch (const std::exception& e) {
        log::error_fmt("WS flush_writes exception: {}", e.what());
        alive_ = false;
        stream_.close();
    }
    if (flushing_) {
        flushing_ = false;
        // ★ 双重检查锁：防 flushing_ 窗口期消息入队但未启动 flush
        //   时序：flush 退出 while → msg 被 queue_send/urgent 入队 → flushing_=false
        //   此时 msg 在队列无人发送，直到下一次 queue_send 才触发
        if (!write_queue_.empty() || !urgent_queue_.empty() || !pending_pong_.empty()) {
            log::debug_fmt("WS flush re-arm: queue={} urgent={} pending_pong={} after reset",
                          write_queue_.size(), urgent_queue_.size(), pending_pong_.size());
            flushing_ = true;
            net::fire_and_forget(stream_.loop(), flush_writes());
        } else {
            log::debug_fmt("WS flush_writes done (flushing reset)");
        }
    }
}

void WsHandler::close(){alive_=false; stream_.close();}

/// 循环读取直到填满缓冲区

net::Task<WsFrame> WsHandler::read_frame() {
    uint8_t h[2];
    auto n1=co_await stream_.read_some(reinterpret_cast<char*>(h),2);
    if(n1==0) throw std::runtime_error("WS closed");
    while(n1<2){auto n=co_await stream_.read_some(reinterpret_cast<char*>(h)+n1,2-n1);if(n==0)throw std::runtime_error("WS closed");n1+=n;}
    WsFrame f; f.fin=(h[0]&0x80)!=0; f.opcode=static_cast<WsOpcode>(h[0]&0x0F);
    bool masked=(h[1]&0x80)!=0; uint64_t len=h[1]&0x7F;
    if(len==126){uint8_t e[2];co_await stream_.read_all(reinterpret_cast<char*>(e),2);len=(uint64_t(e[0])<<8)|e[1];}
    else if(len==127){uint8_t e[8];co_await stream_.read_all(reinterpret_cast<char*>(e),8);len=0;for(int i=0;i<8;++i)len=(len<<8)|e[i];}
    uint32_t mk=0;
    if(masked){uint8_t m[4];co_await stream_.read_all(reinterpret_cast<char*>(m),4);mk=(uint32_t(m[0])<<24)|(uint32_t(m[1])<<16)|(uint32_t(m[2])<<8)|uint32_t(m[3]);}
    constexpr uint64_t MAX=16*1024*1024;
    if(len>MAX){log::error_fmt("WS frame too large: {}",len);alive_=false;co_return f;}
    f.payload.resize(static_cast<size_t>(len));
    if(len>0){co_await stream_.read_all(f.payload.data(),static_cast<size_t>(len));if(masked) apply_mask(reinterpret_cast<uint8_t*>(f.payload.data()),static_cast<size_t>(len),mk);}
    co_return f;
}

net::Task<void> WsHandler::write_frame(WsOpcode opcode,bool fin,std::string_view payload) {
    if(!alive_) co_return;
    auto now = std::chrono::steady_clock::now();
    static auto last_log = now;
    auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log).count();
    last_log = now;
    log::debug_fmt("WS write_frame opcode={:#x} len={} since_last_frame={}ms",
                  static_cast<uint8_t>(opcode), payload.size(), diff_ms);
    uint8_t h[10]; int hl=0;
    h[0]=static_cast<uint8_t>(opcode); if(fin) h[0]|=0x80;
    size_t len=payload.size();
    if(len<=125){h[1]=static_cast<uint8_t>(len);hl=2;}
    else if(len<=65535){h[1]=126;h[2]=uint8_t((len>>8)&0xFF);h[3]=uint8_t(len&0xFF);hl=4;}
    else{h[1]=127;for(int i=0;i<8;++i)h[2+i]=uint8_t((len>>(56-i*8))&0xFF);hl=10;}
    co_await stream_.write_all(std::string_view(reinterpret_cast<char*>(h),hl));
    if(len>0) co_await stream_.write_all(payload);
}

void WsHandler::apply_mask(uint8_t* d,size_t len,uint32_t mk){
    uint8_t m[4]={(uint8_t)((mk>>24)&0xFF),(uint8_t)((mk>>16)&0xFF),(uint8_t)((mk>>8)&0xFF),(uint8_t)(mk&0xFF)};
    for(size_t i=0;i<len;++i) d[i]^=m[i%4];
}

} // namespace ben_gear::server
