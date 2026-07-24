#pragma once

#include "base/memory/pool.hpp"

#include <atomic>
#include <cstddef>
#include <utility>

namespace ben_gear::base::container {

/// 对象池统计信息（线程安全，全部使用 atomic）
struct ObjectPoolStats {
    std::atomic<size_t> total_created{0};
    std::atomic<size_t> total_destroyed{0};
    std::atomic<size_t> active_count{0};

    ObjectPoolStats() = default;
    ObjectPoolStats(const ObjectPoolStats& other)
        : total_created(other.total_created.load(std::memory_order_relaxed))
        , total_destroyed(other.total_destroyed.load(std::memory_order_relaxed))
        , active_count(other.active_count.load(std::memory_order_relaxed)) {}
};

/// 对象池 — 复用对象，减少构造/析构开销
///
/// 直接基于 FixedSizePool（16 路 Shard 分片 + Spinlock）管理内存，
/// 不再额外维护 free_list 和 mutex，避免双重锁开销。
template <typename T>
class ObjectPool {
public:
    ObjectPool() = default;
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /// 创建对象（从 FixedSizePool 取内存 + placement new 构造）
    template <typename... Args>
    T* create(Args&&... args) {
        void* ptr = memory_pool_.allocate();
        T* obj = new (ptr) T(std::forward<Args>(args)...);
        stats_.total_created.fetch_add(1, std::memory_order_relaxed);
        stats_.active_count.fetch_add(1, std::memory_order_relaxed);
        return obj;
    }

    /// 销毁对象（析构 + 归还内存到 FixedSizePool）
    void destroy(T* obj) {
        if (!obj) return;
        obj->~T();
        memory_pool_.deallocate(obj);
        stats_.total_destroyed.fetch_add(1, std::memory_order_relaxed);
        stats_.active_count.fetch_sub(1, std::memory_order_relaxed);
    }

    ObjectPoolStats stats() const { return stats_; }
    size_t active_count() const noexcept {
        return stats_.active_count.load(std::memory_order_relaxed);
    }

private:
    memory::FixedSizePool memory_pool_{sizeof(T)};
    mutable ObjectPoolStats stats_;
};

/// 智能指针包装 — RAII 自动归还对象到池
template <typename T>
class PooledObject {
public:
    PooledObject(T* ptr, ObjectPool<T>* pool) noexcept
        : ptr_(ptr), pool_(pool) {}

    PooledObject(PooledObject&& other) noexcept
        : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_ = nullptr;
        other.pool_ = nullptr;
    }

    ~PooledObject() {
        if (ptr_ && pool_) pool_->destroy(ptr_);
    }

    PooledObject(const PooledObject&) = delete;
    PooledObject& operator=(const PooledObject&) = delete;

    PooledObject& operator=(PooledObject&& other) noexcept {
        if (this != &other) {
            if (ptr_ && pool_) pool_->destroy(ptr_);
            ptr_ = other.ptr_;
            pool_ = other.pool_;
            other.ptr_ = nullptr;
            other.pool_ = nullptr;
        }
        return *this;
    }

    T* get() noexcept { return ptr_; }
    const T* get() const noexcept { return ptr_; }
    T& operator*() noexcept { return *ptr_; }
    const T& operator*() const noexcept { return *ptr_; }
    T* operator->() noexcept { return ptr_; }
    const T* operator->() const noexcept { return ptr_; }
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

    template <typename... Args>
    PooledObject<T> make_pooled(Args&&... args) {
        T* ptr = this->create(std::forward<Args>(args)...);
        return PooledObject<T>(ptr, this);
    }
};

}  // namespace ben_gear::base::container
