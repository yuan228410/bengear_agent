#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include "ben_gear/base/concurrency/spinlock.hpp"
#include "ben_gear/base/concurrency/tid.hpp"

namespace ben_gear::base::memory {

/// 内存池配置
struct PoolConfig {
    size_t small_block_size = 64;      ///< 小块大小（<= 64 字节）
    size_t medium_block_size = 1024;   ///< 中块大小（<= 1KB）
    size_t large_block_size = 65536;   ///< 大块大小（<= 64KB）
    size_t chunk_size = 256;           ///< 每次向系统申请的块数量（增大减少系统调用）
    bool thread_safe = true;           ///< 是否线程安全
};

/// 内存池统计信息（非原子，需在锁内访问或做快照）
struct PoolStats {
    size_t total_allocated = 0;
    size_t total_freed = 0;
    size_t pool_size = 0;
    size_t chunk_count = 0;

    PoolStats() = default;
    PoolStats(const PoolStats&) = default;
    PoolStats& operator=(const PoolStats&) = default;

    void add(const PoolStats& other) {
        total_allocated += other.total_allocated;
        total_freed += other.total_freed;
        pool_size += other.pool_size;
        chunk_count += other.chunk_count;
    }
};

/// 固定大小内存池（分片 + 全局溢出再平衡）
/// 多线程优化：
/// - 16 个独立 Shard，各自有 free_list + Spinlock + chunk 列表
/// - 按线程 ID 哈希到 Shard，锁竞争分散到 16 路
/// - Spinlock 替代 std::mutex，纯用户态自旋，无内核切换
/// - 每个 Shard 独占 chunk 列表，消除全局 chunks_lock_
/// - 全局溢出链表：shard 缓存过多时归还，shard 空时取用
///   解决跨线程 alloc/free 导致的内存堆积问题（Producer-Consumer 场景）
/// - Shard 缓存行对齐（64B），避免 false sharing
class FixedSizePool {
public:
    explicit FixedSizePool(size_t block_size, size_t chunk_size = 256, bool thread_safe = true);
    ~FixedSizePool();

    FixedSizePool(const FixedSizePool&) = delete;
    FixedSizePool& operator=(const FixedSizePool&) = delete;

    FixedSizePool(FixedSizePool&& other) noexcept;
    FixedSizePool& operator=(FixedSizePool&& other) noexcept;

    void* allocate();
    void deallocate(void* ptr);

    PoolStats stats() const;
    size_t block_size() const noexcept { return block_size_; }
    void reset();

private:
    void allocate_chunk(size_t shard_index);
    void try_push_overflow(size_t shard_index);
    struct Block;
    Block* try_pop_overflow();

    struct Block {
        Block* next;
    };

    /// 单个分片：独立的 free list + Spinlock + chunk 存储
    /// 缓存行对齐 + padding，确保不同 shard 不共享缓存行
    struct alignas(64) Shard {
        Block* free_list = nullptr;
        mutable concurrency::Spinlock lock;
        std::vector<void*> chunks;    ///< 本 shard 独占的 chunk 列表
        size_t free_count = 0;        ///< 本 shard 空闲块数量
        size_t allocated = 0;         ///< 本 shard 分配统计
        size_t freed = 0;             ///< 本 shard 释放统计
        size_t pool_size = 0;         ///< 本 shard 内存大小
        size_t chunk_count = 0;       ///< 本 shard chunk 数量
    };

    static constexpr size_t kShardCount = 16;       ///< 分片数，必须为 2 的幂
    static constexpr size_t kShardMask = kShardCount - 1;
    static constexpr size_t kOverflowThreshold = 256; ///< shard 空闲块超过此值时归还溢出链表
    static constexpr size_t kStealBatch = 32;       ///< 从溢出链表一次取的块数

    /// 获取当前线程对应的 shard 索引
    static size_t shard_index() {
        return static_cast<size_t>(concurrency::current_thread_id()) & kShardMask;
    }

    size_t block_size_;
    size_t chunk_size_;
    bool thread_safe_;
    Shard shards_[kShardCount];  ///< 缓存行对齐的分片数组

    /// 全局溢出链表：跨线程再平衡
    /// shard 缓存过多时归还（deallocate 路径）
    /// shard 空时取用（allocate 路径）
    Block* overflow_list_ = nullptr;
    size_t overflow_count_ = 0;
    mutable concurrency::Spinlock overflow_lock_;
};

/// 统一内存池
/// 按大小分桶，自动选择合适的 FixedSizePool
class MemoryPool {
public:
    explicit MemoryPool(const PoolConfig& config = {});
    ~MemoryPool() = default;

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

    PoolStats stats() const;
    void reset();

private:
    static size_t pool_index(size_t size);

    PoolConfig config_;
    std::vector<FixedSizePool> pools_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_freed_{0};
};

/// STL 兼容分配器
template <typename T>
class PoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    explicit PoolAllocator(MemoryPool& pool) : pool_(&pool) {}

    template <typename U>
    PoolAllocator(const PoolAllocator<U>& other) : pool_(other.pool_) {}

    T* allocate(size_type n) {
        return static_cast<T*>(pool_->allocate(n * sizeof(T)));
    }

    void deallocate(T* ptr, size_type n) {
        pool_->deallocate(ptr, n * sizeof(T));
    }

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    bool operator==(const PoolAllocator& other) const { return pool_ == other.pool_; }
    bool operator!=(const PoolAllocator& other) const { return pool_ != other.pool_; }

private:
    MemoryPool* pool_;
    template <typename U> friend class PoolAllocator;
};

/// Arena 分配器
/// 适合批量分配，一次性释放
class Arena {
public:
    explicit Arena(size_t block_size = 4096, bool thread_safe = true);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(size_t size);
    void reset();
    PoolStats stats() const;

private:
    void allocate_block();

    size_t block_size_;
    char* current_block_ = nullptr;
    size_t current_offset_ = 0;
    std::vector<void*> blocks_;
    PoolStats stats_;
    bool thread_safe_;
    mutable concurrency::Spinlock lock_;
};

}  // namespace ben_gear::base::memory
