#pragma once

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/container/object_pool.hpp"
#include "ben_gear/config/settings.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ben_gear::net {

/// HTTP 连接池配置
struct ConnectionPoolConfig {
    std::size_t max_connections_per_host = 10;  // 每个主机最大连接数
    std::chrono::seconds idle_timeout{30};       // 空闲连接超时时间
    std::chrono::seconds connect_timeout{10};    // 连接超时
    std::chrono::seconds response_timeout{60};   // 整体响应超时（含读），防止服务端无响应时永久挂起
    bool enable_keep_alive = true;               // 是否启用 keep-alive
    bool enable_object_pool = true;              // 是否启用对象池
};

/// 从 ConnectionPoolSettings 转换为 ConnectionPoolConfig
inline ConnectionPoolConfig to_pool_config(const config::ConnectionPoolSettings& s) {
    ConnectionPoolConfig cfg;
    cfg.max_connections_per_host = static_cast<std::size_t>(s.max_connections_per_host);
    cfg.idle_timeout = std::chrono::seconds(s.idle_timeout_seconds);
    cfg.connect_timeout = std::chrono::seconds(s.connect_timeout_seconds);
    cfg.response_timeout = std::chrono::seconds(s.response_timeout_seconds);
    cfg.enable_keep_alive = s.enable_keep_alive;
    cfg.enable_object_pool = s.enable_object_pool;
    return cfg;
}

/// 连接池中的连接条目
struct PooledConnection {
    TcpStream stream;
    std::chrono::steady_clock::time_point last_used;
    bool in_use = false;
    void* tls_state = nullptr;  // SSL*（TLS 连接池化），由 http.hpp Transport 管理

    explicit PooledConnection(TcpStream s, void* tls = nullptr)
        : stream(std::move(s)), last_used(std::chrono::steady_clock::now()), tls_state(tls) {}
};

/// 连接池键（协议+主机+端口）
struct ConnectionKey {
    bool tls = false;
    std::string host;
    std::string port;

    bool operator==(const ConnectionKey& other) const {
        return tls == other.tls && host == other.host && port == other.port;
    }
};

/// ConnectionKey 哈希函数
struct ConnectionKeyHash {
    std::size_t operator()(const ConnectionKey& key) const {
        return std::hash<std::string>()(key.host) ^
               (std::hash<std::string>()(key.port) << 1) ^
               (std::hash<bool>()(key.tls) << 2);
    }
};

/// HTTP 连接池（线程安全）
class ConnectionPool {
public:
    explicit ConnectionPool(ConnectionPoolConfig config = {});
    ~ConnectionPool();
    
    /// 从池中获取连接（如果没有可用连接则创建新连接）
    /// 返回 {TcpStream, tls_state}，tls_state 非 null 表示已有 TLS 状态可直接复用
    Task<std::pair<TcpStream, void*>> acquire(EventLoop& loop, bool tls, const std::string& host, const std::string& port);

    /// 将连接归还到池中
    void release(bool tls, const std::string& host, const std::string& port, TcpStream stream, void* tls_state = nullptr);
    
    /// 清理空闲超时的连接
    void cleanup_idle();
    
    /// 获取当前池中的连接数
    std::size_t size() const;

    /// 获取配置
    const ConnectionPoolConfig& config() const noexcept { return config_; }
    
    /// 获取指定主机的连接数
    std::size_t size(const std::string& host, const std::string& port) const;
    std::size_t size(bool tls, const std::string& host, const std::string& port) const;
    
    /// 获取对象池统计信息
    base::container::ObjectPoolStats object_pool_stats() const;

    /// 预热连接池：为指定主机预先创建连接
    /// @param loop 事件循环
    /// @param tls 是否使用 TLS
    /// @param host 主机名
    /// @param port 端口
    /// @param count 预热连接数
    Task<void> warmup(EventLoop& loop, bool tls, const std::string& host, const std::string& port, size_t count);

private:
    ConnectionPoolConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<ConnectionKey, std::deque<PooledConnection*>, ConnectionKeyHash> pools_;
    
    /// 对象池（用于连接对象复用）
    std::unique_ptr<base::container::ObjectPool<PooledConnection>> object_pool_;
};

}  // namespace ben_gear::net
