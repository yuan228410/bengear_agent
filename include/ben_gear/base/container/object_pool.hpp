#pragma once

#include "ben_gear/base/memory/pool.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

namespace ben_gear::base::container {

/// 对象池配置
struct ObjectPoolConfig {
    size_t chunk_size = 64;            ///< 每次分配的对象数
    bool thread_safe = true;           ///< 是否线程安全
    bool enable_statistics = true;     ///< 是否启用统计
};

/// 对象池统计信息（线程安全，全部使用 atomic）
struct ObjectPoolStats {
    std::atomic<size_t> total_created{0};
    std::atomic<size_t> total_destroyed{0};
    std::atomic<size_t> pool_size{0};
    std::atomic<size_t> active_count{0};

    ObjectPoolStats() = default;
    ObjectPoolStats(const ObjectPoolStats& other)
        : total_created(other.total_created.load(std::memory_order_relaxed))
        , total_destroyed(other.total_destroyed.load(std::memory_order_relaxed))
        , pool_size(other.pool_size.load(std::memory_order_relaxed))
        , active_count(other.active_count.load(std::memory_order_relaxed)) {}
};

/// 对象池
/// 复用对象，减少构造/析构开销
/// 
/// 使用示例：
/// ```cpp
/// ben_gear::base::container::ObjectPool<Connection> pool;
/// 
/// // 创建对象
/// Connection* conn = pool.create("localhost", 8080);
/// 
/// // 使用对象
/// conn->send("Hello");
/// 
/// // 销毁对象
/// pool.destroy(conn);
/// ```
template <typename T>
class ObjectPool {
public:
    /// 构造函数
    explicit ObjectPool(const ObjectPoolConfig& config = {})
        : config_(config)
        , memory_pool_(sizeof(ObjectNode)) {
    }
    
    /// 析构函数
    ~ObjectPool() {
        // 清理所有空闲对象
        clear();
    }
    
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    
    /// 创建对象
    /// @param args 构造参数
    /// @return 对象指针
    template <typename... Args>
    T* create(Args&&... args) {
        ObjectNode* node = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // 从空闲链表获取
            if (free_list_) {
                node = free_list_;
                free_list_ = free_list_->next;
                stats_.pool_size.fetch_sub(1, std::memory_order_relaxed);
            }
        }

        // 如果没有空闲对象，分配新对象
        if (!node) {
            void* ptr = memory_pool_.allocate();
            node = new (ptr) ObjectNode();
        }

        // 构造对象
        T* obj = new (&node->object) T(std::forward<Args>(args)...);
        node->in_use = true;

        // 更新统计（atomic，无需加锁）
        stats_.total_created.fetch_add(1, std::memory_order_relaxed);
        stats_.active_count.fetch_add(1, std::memory_order_relaxed);

        return obj;
    }

    /// 销毁对象
    /// @param obj 对象指针
    void destroy(T* obj) {
        if (!obj) return;

        // 获取对象节点
        ObjectNode* node = reinterpret_cast<ObjectNode*>(
            reinterpret_cast<char*>(obj) - offsetof(ObjectNode, object)
        );

        // 析构对象
        obj->~T();
        node->in_use = false;

        // 加入空闲链表
        {
            std::lock_guard<std::mutex> lock(mutex_);
            node->next = free_list_;
            free_list_ = node;
            stats_.pool_size.fetch_add(1, std::memory_order_relaxed);
        }

        // 更新统计（atomic，无需加锁）
        stats_.total_destroyed.fetch_add(1, std::memory_order_relaxed);
        stats_.active_count.fetch_sub(1, std::memory_order_relaxed);
    }
    
    /// 清空池
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);

        // 释放所有空闲对象
        while (free_list_) {
            ObjectNode* node = free_list_;
            free_list_ = free_list_->next;
            node->~ObjectNode();
            memory_pool_.deallocate(node);
        }

        stats_.pool_size.store(0, std::memory_order_relaxed);
    }

    /// 获取统计信息
    ObjectPoolStats stats() const {
        return stats_;
    }

    /// 获取活跃对象数
    size_t active_count() const noexcept {
        return stats_.active_count.load(std::memory_order_relaxed);
    }

private:
    /// 对象节点
    struct ObjectNode {
        alignas(T) char object[sizeof(T)];  ///< 对象存储
        ObjectNode* next = nullptr;          ///< 下一个节点
        bool in_use = false;                 ///< 是否在使用
    };
    
    ObjectPoolConfig config_;
    memory::FixedSizePool memory_pool_;
    ObjectNode* free_list_ = nullptr;
    mutable std::mutex mutex_;
    mutable ObjectPoolStats stats_;
};

/// 智能指针包装
/// 自动归还对象到池
template <typename T>
class PooledObject {
public:
    /// 构造函数
    PooledObject(T* ptr, ObjectPool<T>* pool) noexcept
        : ptr_(ptr), pool_(pool) {}
    
    /// 移动构造
    PooledObject(PooledObject&& other) noexcept
        : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_ = nullptr;
        other.pool_ = nullptr;
    }
    
    /// 析构函数
    ~PooledObject() {
        if (ptr_ && pool_) {
            pool_->destroy(ptr_);
        }
    }
    
    PooledObject(const PooledObject&) = delete;
    PooledObject& operator=(const PooledObject&) = delete;
    
    PooledObject& operator=(PooledObject&& other) noexcept {
        if (this != &other) {
            if (ptr_ && pool_) {
                pool_->destroy(ptr_);
            }
            ptr_ = other.ptr_;
            pool_ = other.pool_;
            other.ptr_ = nullptr;
            other.pool_ = nullptr;
        }
        return *this;
    }
    
    /// 获取指针
    T* get() noexcept { return ptr_; }
    const T* get() const noexcept { return ptr_; }
    
    /// 解引用
    T& operator*() noexcept { return *ptr_; }
    const T& operator*() const noexcept { return *ptr_; }
    
    /// 箭头操作符
    T* operator->() noexcept { return ptr_; }
    const T* operator->() const noexcept { return ptr_; }
    
    /// 布尔转换
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_;
    ObjectPool<T>* pool_;
};

/// 对象池扩展：支持 make_pooled
template <typename T>
class ObjectPoolWithMakePooled : public ObjectPool<T> {
public:
    using ObjectPool<T>::ObjectPool;
    
    /// 创建智能指针对象
    template <typename... Args>
    PooledObject<T> make_pooled(Args&&... args) {
        T* ptr = this->create(std::forward<Args>(args)...);
        return PooledObject<T>(ptr, this);
    }
};

}  // namespace ben_gear::base::container
