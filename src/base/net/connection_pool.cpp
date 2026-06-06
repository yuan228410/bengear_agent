#include "ben_gear/base/net/connection_pool.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/socket.hpp"

#include <openssl/ssl.h>

namespace ben_gear::net {

ConnectionPool::ConnectionPool(ConnectionPoolConfig config)
    : config_(std::move(config)) {
    if (config_.enable_object_pool) {
        object_pool_ = std::make_unique<base::container::ObjectPool<PooledConnection>>();
    }
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard lock(mutex_);
    for (auto& [key, pool] : pools_) {
        for (auto* conn : pool) {
            if (conn->tls_state) {
                SSL_free(static_cast<SSL*>(conn->tls_state));
            }
            if (object_pool_) {
                object_pool_->destroy(conn);
            } else {
                delete conn;
            }
        }
    }
    // destroy() already returns objects to the pool; no need for clear()
    pools_.clear();
}

Task<std::pair<TcpStream, void*>> ConnectionPool::acquire(EventLoop& loop, bool tls, const std::string& host, const std::string& port) {
    // 先尝试从池中获取可用连接
    {
        std::lock_guard lock(mutex_);
        ConnectionKey key{tls, host, port};
        auto it = pools_.find(key);

        if (it != pools_.end() && !it->second.empty()) {
            // 优化：从后向前扫描，避免 erase 后迭代器失效
            // 使用惰性淘汰：只在连接失效时才删除
            auto& pool = it->second;
            for (int i = static_cast<int>(pool.size()) - 1; i >= 0; --i) {
                auto& conn = pool[i];
            if (!conn->in_use && conn->stream.valid()) {
                    // 惰性淘汰：检查空闲时间和连接存活
                    auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - conn->last_used);
                    bool expired = idle_duration >= config_.idle_timeout;

                    auto stream = std::move(conn->stream);
                    auto* tls_ptr = conn->tls_state;
                    conn->tls_state = nullptr;
                    
                    // 删除连接对象
                    if (object_pool_) {
                        object_pool_->destroy(conn);
                    } else {
                        delete conn;
                    }
                    pool.erase(pool.begin() + i);
                    
                    // 空闲超时或连接已死，丢弃并继续查找
                    if (expired || !is_socket_alive(stream.native_handle())) {
                        // 连接已失效，清理并继续
                        if (tls_ptr) {
                            SSL_free(static_cast<SSL*>(tls_ptr));
                        }
                        log::info_fmt("pool: acquire discarded {} connection to {}:{} tls={} idle={}s",
                                      expired ? "expired" : "dead", host, port, tls ? "yes" : "no", idle_duration.count());
                        continue;  // 继续查找下一个可用连接
                    }
                    
                    log::info_fmt("pool: acquire reused connection from {}:{} tls={}", host, port, tls ? "yes" : "no");
                    co_return std::make_pair(std::move(stream), tls_ptr);
                }
            }
        }
    }

    // 池中没有可用连接，创建新连接
    auto stream = co_await async_connect(loop, host, port, config_.connect_timeout);
    log::info_fmt("pool: acquire new connection to {}:{}", host, port);
    co_return std::make_pair(std::move(stream), nullptr);
}

void ConnectionPool::release(bool tls, const std::string& host, const std::string& port, TcpStream stream, void* tls_state) {
    if (!config_.enable_keep_alive || !stream.valid()) {
        if (tls_state) {
            SSL_free(static_cast<SSL*>(tls_state));
        }
        log::info_fmt("pool: release dropped connection to {}:{} tls={} (keep_alive={} valid={})",
                      host, port, tls_state ? "yes" : "no", config_.enable_keep_alive, stream.valid());
        return;
    }

    std::lock_guard lock(mutex_);
    ConnectionKey key{tls, host, port};
    auto& pool = pools_[key];

    size_t idle_count = 0;
    for (const auto* conn : pool) {
        if (!conn->in_use) idle_count++;
    }

    if (idle_count >= config_.max_connections_per_host) {
        if (tls_state) {
            SSL_free(static_cast<SSL*>(tls_state));
        }
        log::info_fmt("pool: release dropped connection to {}:{} tls={} (pool full idle={})", host, port, tls_state ? "yes" : "no", idle_count);
        return;
    }

    PooledConnection* conn = nullptr;
    if (object_pool_) {
        conn = object_pool_->create(std::move(stream), tls_state);
    } else {
        conn = new PooledConnection(std::move(stream), tls_state);
    }
    conn->in_use = false;
    conn->last_used = std::chrono::steady_clock::now();
    pool.push_back(conn);
    log::info_fmt("pool: release returned connection to {}:{} tls={} idle={}", host, port, tls_state ? "yes" : "no", idle_count + 1);
}

void ConnectionPool::cleanup_idle() {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto& [key, pool] : pools_) {
        auto it = pool.begin();
        while (it != pool.end()) {
            if (!(*it)->in_use) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - (*it)->last_used);
                bool dead = !(*it)->stream.valid() || !is_socket_alive((*it)->stream.native_handle());
                if (idle_time >= config_.idle_timeout || dead) {
                    if ((*it)->tls_state) {
                        SSL_free(static_cast<SSL*>((*it)->tls_state));
                    }
                    if (object_pool_) {
                        object_pool_->destroy(*it);
                    } else {
                        delete *it;
                    }
                    it = pool.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
}

std::size_t ConnectionPool::size() const {
    std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto& [key, pool] : pools_) {
        total += pool.size();
    }
    return total;
}

std::size_t ConnectionPool::size(const std::string& host, const std::string& port) const {
    return size(false, host, port) + size(true, host, port);
}

std::size_t ConnectionPool::size(bool tls, const std::string& host, const std::string& port) const {
    std::lock_guard lock(mutex_);
    ConnectionKey key{tls, host, port};
    auto it = pools_.find(key);
    return it != pools_.end() ? it->second.size() : 0;
}

base::container::ObjectPoolStats ConnectionPool::object_pool_stats() const {
    if (object_pool_) {
        return object_pool_->stats();
    }
    return {};
}

Task<void> ConnectionPool::warmup(EventLoop& loop, bool tls, const std::string& host, const std::string& port, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto stream = co_await async_connect(loop, host, port, config_.connect_timeout);
        release(tls, host, port, std::move(stream), nullptr);
    }
    log::info_fmt("pool: warmup created {} connections to {}:{} tls={}", count, host, port, tls ? "yes" : "no");
}

}  // namespace ben_gear::net
