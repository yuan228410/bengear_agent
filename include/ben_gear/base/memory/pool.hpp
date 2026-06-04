#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
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
    size_t total_allocated = 0;    ///< 总分配字节数
    size_t total_freed = 0;        ///< 总释放字节数
    size_t pool_size = 0;          ///< 池大小
    size_t chunk_count = 0;        ///< 块数量
};

/// 固定大小内存池
/// 用于分配固定大小的内存块，性能最优
class FixedSizePool {
public:
    /// 构造函数
    /// @param block_size 每个块的大小
    /// @param chunk_size 每次向系统申请的块数量
    explicit FixedSizePool(size_t block_size, size_t chunk_size = 64);
    
    ~FixedSizePool();
    
    FixedSizePool(const FixedSizePool&) = delete;
    FixedSizePool& operator=(const FixedSizePool&) = delete;
    
    FixedSizePool(FixedSizePool&& other) noexcept
        : block_size_(other.block_size_)
        , chunk_size_(other.chunk_size_)
        , free_list_(other.free_list_)
        , chunks_(std::move(other.chunks_))
        , stats_(other.stats_) {
        other.free_list_ = nullptr;
        other.chunks_.clear();
        other.stats_ = PoolStats{};
    }
    
    FixedSizePool& operator=(FixedSizePool&& other) noexcept {
        if (this != &other) {
            // 释放当前资源
            for (void* chunk : chunks_) {
                ::operator delete(chunk);
            }
            
            block_size_ = other.block_size_;
            chunk_size_ = other.chunk_size_;
            free_list_ = other.free_list_;
            chunks_ = std::move(other.chunks_);
            stats_ = other.stats_;
            
            other.free_list_ = nullptr;
            other.chunks_.clear();
            other.stats_ = PoolStats{};
        }
        return *this;
    }
    
    /// 分配一个块
    void* allocate();
    
    /// 释放一个块
    void deallocate(void* ptr);
    
    /// 获取统计信息
    PoolStats stats() const;
    
    /// 获取块大小
    size_t block_size() const noexcept { return block_size_; }

private:
    /// 分配新块
    void allocate_chunk();
    
    struct Block {
        Block* next;
    };
    
    size_t block_size_;          ///< 块大小
    size_t chunk_size_;          ///< 每次分配的块数量
    Block* free_list_ = nullptr; ///< 空闲链表
    std::vector<void*> chunks_;  ///< 所有分配的块
    mutable std::mutex mutex_;   ///< 互斥锁
    PoolStats stats_;            ///< 统计信息
};

/// 统一内存池
/// 支持不同大小的内存分配，自动选择合适的池
class MemoryPool {
public:
    /// 构造函数
    explicit MemoryPool(const PoolConfig& config = {});
    
    ~MemoryPool() = default;
    
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    /// 分配内存
    /// @param size 内存大小
    /// @return 内存指针
    void* allocate(size_t size);
    
    /// 释放内存
    /// @param ptr 内存指针
    /// @param size 内存大小
    void deallocate(void* ptr, size_t size);
    
    /// 获取统计信息
    PoolStats stats() const;
    
    /// 重置（释放所有内存）
    void reset();

private:
    /// 根据大小选择池索引
    static size_t pool_index(size_t size);
    
    PoolConfig config_;
    std::vector<FixedSizePool> pools_;  ///< 不同大小的池
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_freed_{0};
};

/// STL 兼容分配器
/// 可以与 STL 容器配合使用
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
    
    /// 构造函数
    explicit PoolAllocator(MemoryPool& pool) : pool_(&pool) {}
    
    /// 拷贝构造
    template <typename U>
    PoolAllocator(const PoolAllocator<U>& other) : pool_(other.pool_) {}
    
    /// 分配内存
    T* allocate(size_type n) {
        return static_cast<T*>(pool_->allocate(n * sizeof(T)));
    }
    
    /// 释放内存
    void deallocate(T* ptr, size_type n) {
        pool_->deallocate(ptr, n * sizeof(T));
    }
    
    /// 重新绑定类型
    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };
    
    /// 比较运算符
    bool operator==(const PoolAllocator& other) const {
        return pool_ == other.pool_;
    }
    
    bool operator!=(const PoolAllocator& other) const {
        return pool_ != other.pool_;
    }

private:
    MemoryPool* pool_;
    
    template <typename U>
    friend class PoolAllocator;
};

/// Arena 分配器
/// 适合批量分配，一次性释放
/// 当 PoolConfig::thread_safe=true 时使用 mutex 保护
class Arena {
public:
    /// 构造函数
    /// @param block_size 每个块的大小
    /// @param thread_safe 是否线程安全
    explicit Arena(size_t block_size = 4096, bool thread_safe = true);

    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    /// 分配内存
    /// @param size 内存大小
    /// @return 内存指针
    void* allocate(size_t size);

    /// 重置（释放所有内存）
    void reset();

    /// 获取统计信息
    PoolStats stats() const;

private:
    /// 分配新块
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
