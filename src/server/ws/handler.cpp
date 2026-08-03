#include "server/ws/handler.hpp"
#include <mutex>
#include "net/event_loop.hpp"
#include "log/logger.hpp"
#include "platform/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace ben_gear::server {

static const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
static constexpr size_t kMaxQueuedMessages = 1024;
static constexpr size_t kMaxQueuedBytes = 16 * 1024 * 1024;

std::string compute_ws_accept(const std::string& ws_key) {
    std::string combined = ws_key + WS_MAGIC;
    return base::platform::sha1_base64(combined);
}

bool is_ws_upgrade(const std::string& method, const std::string&,
                   const std::map<std::string, std::string>& headers) {
    if(method!="GET") return false;
    auto it=headers.find("upgrade"); if(it==headers.end()) return false;
    std::string v=it->second; std::transform(v.begin(),v.end(),v.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    if(v!="websocket") return false;
    // RFC 6455 §4.1: 也必须验证 Connection: Upgrade
    auto cit=headers.find("connection"); if(cit==headers.end()) return false;
    std::string cv=cit->second; std::transform(cv.begin(),cv.end(),cv.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return cv.find("upgrade") != std::string::npos;
}

WsHandler::WsHandler(net::TcpStream stream, std::string ws_key)
    : stream_(std::move(stream)), ws_key_(std::move(ws_key)) {}

net::Task<void> WsHandler::handshake(const std::string& origin) {
    auto accept_key = compute_ws_accept(ws_key_);
    std::string resp;
    resp.append("HTTP/1.1 101 Switching Protocols\r\n");
    resp.append("Upgrade: websocket\r\nConnection: Upgrade\r\n");
    resp.append("Sec-WebSocket-Accept: ");
    resp.append(accept_key);
    resp.append("\r\n");
    if(!origin.empty()){resp.append("Access-Control-Allow-Origin: ");resp.append(origin);resp.append("\r\n");}
    resp.append("\r\n");
    co_await stream_.write_all(std::string_view(resp.data(),resp.size()));
    log::debug_fmt("WS handshake completed");
}

net::Task<void> WsHandler::send_text(std::string_view msg){co_await write_frame(WsOpcode::text,true,msg);}
net::Task<void> WsHandler::send_binary(std::string_view d){co_await write_frame(WsOpcode::binary,true,d);}
net::Task<void> WsHandler::send_pong(std::string_view p){co_await write_frame(WsOpcode::pong,true,p);}

net::Task<void> WsHandler::send_close(uint16_t code,std::string_view reason){
    if(!alive_) co_return;
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
                {
                    std::lock_guard lk(write_mutex_);
                    pending_pong_ = frame.payload;
                }
                // flush_writes 可能处于 idle 状态，需要确保它被唤醒
                if (!flushing_.exchange(true)) {
                    net::fire_and_forget(stream_.loop(), flush_writes());
                }
                break;
            case WsOpcode::close: alive_=false; break;
            default: break;
            }
        }
    } catch(const std::exception& e){
        log::error_fmt("WS read_loop exception: {} alive={} fd={}",
            e.what(), alive_.load(), stream_.native_handle());
        alive_=false;
        stream_.close();
    }
    log::info_fmt("WS read_loop exiting: alive={} fd={}", alive_.load(), stream_.native_handle());
    alive_=false;
    stream_.close();
    if(on_close) on_close();
}

void WsHandler::queue_send(std::string json) {
    if (!alive_) return;
    {
        std::lock_guard lk(write_mutex_);
        if (write_queue_.size() >= kMaxQueuedMessages || queued_bytes_ + json.size() > kMaxQueuedBytes) {
            log::warn_fmt("WS write queue limit exceeded: queue={} bytes={} incoming={}",
                          write_queue_.size(), queued_bytes_, json.size());
            alive_ = false;
            // 在 EventLoop 线程上关闭流，避免跨线程 resume 协程
            // 捕获 shared_from_this 保证 lambda 执行时 WsHandler 仍存活
            auto self = shared_from_this();
            stream_.loop().submit_task([self]() { self->stream_.close(); });
            return;
        }
        queued_bytes_ += json.size();
        write_queue_.push_back(std::move(json));
    }
    // 如果没有正在执行的 flush，启动一个（flushing_ 是 atomic，compare_exchange 保证只启动一个）
    if (!flushing_.exchange(true)) {
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

void WsHandler::queue_send_front(std::string json) {
    if (!alive_) return;
    {
        std::lock_guard lk(write_mutex_);
        if (write_queue_.size() >= kMaxQueuedMessages || queued_bytes_ + json.size() > kMaxQueuedBytes) {
            log::warn_fmt("WS write queue limit exceeded at front: queue={} bytes={} incoming={}",
                          write_queue_.size(), queued_bytes_, json.size());
            alive_ = false;
            // 在 EventLoop 线程上关闭流，避免跨线程 resume 协程
            auto self = shared_from_this();
            stream_.loop().submit_task([self]() { self->stream_.close(); });
            return;
        }
        queued_bytes_ += json.size();
        write_queue_.push_front(std::move(json));
    }
    if (!flushing_.exchange(true)) {
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

void WsHandler::queue_send_urgent(std::string json) {
    if (!alive_) return;
    {
        std::lock_guard lk(write_mutex_);
        if (urgent_queue_.size() >= kMaxQueuedMessages || queued_bytes_ + json.size() > kMaxQueuedBytes) {
            log::warn_fmt("WS urgent queue limit exceeded: queue={} bytes={} incoming={}",
                          urgent_queue_.size(), queued_bytes_, json.size());
            alive_ = false;
            // 在 EventLoop 线程上关闭流，避免跨线程 resume 协程
            auto self = shared_from_this();
            stream_.loop().submit_task([self]() { self->stream_.close(); });
            return;
        }
        queued_bytes_ += json.size();
        urgent_queue_.push_back(std::move(json));
    }
    if (!flushing_.exchange(true)) {
        net::fire_and_forget(stream_.loop(), flush_writes());
    }
}

net::Task<void> WsHandler::flush_writes() {
    // 单协程顺序 flush 写队列，保证 WS 帧不交错
    // 每发完一帧优先检查 urgent_queue_（控制帧如 pong），确保不被大 token 阻塞
    try {
        while (alive_) {
            // ★ 检查挂起的 ping 级 pong 帧（由 read_loop 设置，需加锁保护）
            {
                std::lock_guard lk(write_mutex_);
                if (!pending_pong_.empty()) {
                    auto payload = std::move(pending_pong_);
                    pending_pong_.clear();
                    co_await write_frame(WsOpcode::pong, true, payload);
                }
            }
            // 紧急队列绝对优先：每帧之间检查，确保控制帧不被阻塞
            while (alive_) {
                std::string msg;
                {
                    std::lock_guard lk(write_mutex_);
                    if (urgent_queue_.empty()) break;
                    msg = std::move(urgent_queue_.front());
                    queued_bytes_ -= std::min(queued_bytes_, msg.size());
                    urgent_queue_.pop_front();
                }
                co_await send_text(msg);
            }
            // 普通写队列
            std::string msg;
            {
                std::lock_guard lk(write_mutex_);
                if (write_queue_.empty()) break;
                msg = std::move(write_queue_.front());
                queued_bytes_ -= std::min(queued_bytes_, msg.size());
                write_queue_.pop_front();
            }
            co_await send_text(msg);
        }
    } catch (const std::exception& e) {
        log::error_fmt("WS flush_writes exception: {}", e.what());
        alive_ = false;
        stream_.close();
    }
    // ★ flushing_ 重置 + 双重检查：用 atomic exchange 避免窗口期丢消息
    flushing_ = false;
    {
        std::lock_guard lk(write_mutex_);
        if (!write_queue_.empty() || !urgent_queue_.empty() || !pending_pong_.empty()) {
            if (!flushing_.exchange(true)) {
                net::fire_and_forget(stream_.loop(), flush_writes());
            }
        } else {
        }
    }
}

void WsHandler::close(){
    alive_=false;
    stream_.close();
}

/// 循环读取直到填满缓冲区

net::Task<WsFrame> WsHandler::read_frame() {
    uint8_t h[2];
    auto n1=co_await stream_.read_some(reinterpret_cast<char*>(h),2);
    if(n1==0) {
        log::warn_fmt("WS read_frame: read_some returned 0 bytes (EOF) fd={}", stream_.native_handle());
        throw std::runtime_error("WS closed");
    }
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
