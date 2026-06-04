#include "ben_gear/base/net/connection_pool.hpp"

namespace ben_gear::net {

ConnectionPool::ConnectionPool(ConnectionPoolConfig config)
    : config_(std::move(config)) {
    if (config_.enable_object_pool) {
        object_pool_ = std::make_unique<base::container::ObjectPool<PooledConnection>>();
    }
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard lock(mutex_);
    if (object_pool_) {
        // ObjectPool 管理内存，先析构所有活跃对象，再清空池
        for (auto& [key, pool] : pools_) {
            for (auto* conn : pool) {
                object_pool_->destroy(conn);
            }
        }
        object_pool_->clear();
    } else {
        for (auto& [key, pool] : pools_) {
            for (auto* conn : pool) {
                delete conn;
            }
        }
    }
    pools_.clear();
}

Task<TcpStream> ConnectionPool::acquire(EventLoop& loop, const std::string& host, const std::string& port) {
    // 先尝试从池中获取可用连接
    {
        std::lock_guard lock(mutex_);
        ConnectionKey key{host, port};
        auto it = pools_.find(key);

        if (it != pools_.end() && !it->second.empty()) {
            for (auto conn_it = it->second.begin(); conn_it != it->second.end(); ++conn_it) {
                if (!(*conn_it)->in_use && (*conn_it)->stream.valid()) {
                    auto stream = std::move((*conn_it)->stream);
                    if (object_pool_) {
                        object_pool_->destroy(*conn_it);
                    } else {
                        delete *conn_it;
                    }
                    it->second.erase(conn_it);
                    co_return stream;
                }
            }
        }
    }

    // 池中没有可用连接，创建新连接（不持锁，避免持锁做 I/O）
    auto stream = co_await async_connect(loop, host, port);

    co_return std::move(stream);
}

void ConnectionPool::release(const std::string& host, const std::string& port, TcpStream stream) {
    if (!config_.enable_keep_alive || !stream.valid()) {
        return;
    }

    std::lock_guard lock(mutex_);
    ConnectionKey key{host, port};
    auto& pool = pools_[key];

    // 只计算空闲连接数，避免丢弃活跃连接归还的连接
    size_t idle_count = 0;
    for (const auto* conn : pool) {
        if (!conn->in_use) idle_count++;
    }

    if (idle_count >= config_.max_connections_per_host) {
        return;
    }

    // 归还连接到池中
    PooledConnection* conn = nullptr;
    if (object_pool_) {
        conn = object_pool_->create(std::move(stream));
    } else {
        conn = new PooledConnection(std::move(stream));
    }
    conn->in_use = false;
    conn->last_used = std::chrono::steady_clock::now();
    pool.push_back(conn);
}

void ConnectionPool::cleanup_idle() {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto& [key, pool] : pools_) {
        auto it = pool.begin();
        while (it != pool.end()) {
            if (!(*it)->in_use) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - (*it)->last_used);
                if (idle_time >= config_.idle_timeout || !(*it)->stream.valid()) {
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
    std::lock_guard lock(mutex_);
    ConnectionKey key{host, port};
    auto it = pools_.find(key);
    return it != pools_.end() ? it->second.size() : 0;
}

base::container::ObjectPoolStats ConnectionPool::object_pool_stats() const {
    if (object_pool_) {
        return object_pool_->stats();
    }
    return {};
}

}  // namespace ben_gear::net
