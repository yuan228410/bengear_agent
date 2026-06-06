#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace ben_gear::base::memory {

/// 内存池配置
struct PoolConfig {
    size_t small_block_size = 64;      ///< 小块大小（<= 64 字节）
    size_t medium_block_size = 1024;   ///< 中块大小（<= 1KB）
    size_t large_block_size = 65536;   ///< 大块大小（<= 64KB）
    size_t chunk_size = 4096;          ///< 每次向系统申请的块大小
    bool thread_safe = true;           ///< 是否线程安全
};

/// 内存池统计信息
struct PoolStats {
    std::atomic<size_t> total_allocated{0};
    std::atomic<size_t> total_freed{0};
    std::atomic<size_t> pool_size{0};
    std::atomic<size_t> chunk_count{0};

    PoolStats() = default;
    PoolStats(const PoolStats& other)
        : total_allocated(other.total_allocated.load(std::memory_order_relaxed))
        , total_freed(other.total_freed.load(std::memory_order_relaxed))
        , pool_size(other.pool_size.load(std::memory_order_relaxed))
        , chunk_count(other.chunk_count.load(std::memory_order_relaxed)) {}
    PoolStats& operator=(const PoolStats& other) {
        if (this != &other) {
            total_allocated.store(other.total_allocated.load(std::memory_order_relaxed), std::memory_order_relaxed);
            total_freed.store(other.total_freed.load(std::memory_order_relaxed), std::memory_order_relaxed);
            pool_size.store(other.pool_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
            chunk_count.store(other.chunk_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }
};

/// 固定大小内存池
/// 简单设计：mutex + 空闲链表 + chunk 批量分配
class FixedSizePool {
public:
    explicit FixedSizePool(size_t block_size, size_t chunk_size = 64);
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
    void allocate_chunk();

    struct Block {
        Block* next;
    };

    size_t block_size_;
    size_t chunk_size_;
    Block* free_list_ = nullptr;       ///< 空闲链表头
    std::vector<void*> chunks_;        ///< 所有分配的 chunk
    mutable std::mutex mutex_;         ///< 全局互斥锁
    PoolStats stats_;
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
    mutable std::mutex mutex_;
};

}  // namespace ben_gear::base::memory
