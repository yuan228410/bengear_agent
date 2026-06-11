#pragma once

#include "ben_gear/base/net/tls/tls_config.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace ben_gear::net {

/// TLS 引擎抽象接口
///
/// 提供后端无关的 TLS 操作：握手、加密读写、原生句柄管理。
/// 编译期通过 CMake 选项选择后端（MbedTLS / OpenSSL / Schannel / none），
/// 运行时可通过 set_global_tls_engine() 替换。
class TlsEngine {
public:
    virtual ~TlsEngine() = default;

    /// TLS 会话（对应一次 TLS 连接）
    class Session {
    public:
        virtual ~Session() = default;

        /// TLS 握手（协程，可被 Epoll/Kqueue/IOCP 挂起）
        /// @param loop  事件循环
        /// @param fd    已连接的 socket 句柄
        /// @param host  SNI 主机名
        /// @param config TLS 配置
        virtual Task<void> handshake(EventLoop& loop, socket_handle fd,
                                     std::string_view host,
                                     const TlsConfig& config) = 0;

        /// 加密写入全部数据
        virtual Task<void> write_all(EventLoop& loop, std::string_view data) = 0;

        /// 加密读取
        virtual Task<std::size_t> read_some(EventLoop& loop,
                                            char* buf, std::size_t size) = 0;

        /// 原生句柄（用于连接池传递，后端无关的 void*）
        virtual void* native_handle() noexcept = 0;

        /// 优雅关闭（close_notify）
        virtual void shutdown() noexcept = 0;

        /// 是否已完成握手
        virtual bool is_connected() const noexcept = 0;
    };

    /// 创建新的 TLS 会话
    virtual std::unique_ptr<Session> create_session() = 0;

    /// 全局初始化（仅调用一次）
    virtual void initialize() = 0;

    /// 后端名称（用于日志）
    virtual const char* name() const noexcept = 0;

    /// 释放连接池中的 TLS 原生句柄
    /// 替代 static_cast<SSL*> + SSL_free()，实现后端无关的资源释放
    virtual void free_native_handle(void* handle) noexcept = 0;
};

/// 获取全局 TlsEngine 实例（延迟初始化，首次调用时创建默认后端）
TlsEngine& global_tls_engine();

/// 设置全局 TlsEngine 实例（必须在使用前调用）
void set_global_tls_engine(std::unique_ptr<TlsEngine> engine);

/// 创建编译期默认后端
/// macOS/Linux → MbedTLS, Windows → Schannel, TLS_BACKEND=openssl → OpenSSL
std::unique_ptr<TlsEngine> create_default_tls_engine();

}  // namespace ben_gear::net
