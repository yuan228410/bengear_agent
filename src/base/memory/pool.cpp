#include "ben_gear/base/memory/pool.hpp"

namespace ben_gear::base::memory {

// ==================== FixedSizePool ====================

FixedSizePool::FixedSizePool(size_t block_size, size_t chunk_size)
    : block_size_(std::max(block_size, sizeof(void*)))
    , chunk_size_(chunk_size) {
    // 确保块大小至少能存放一个指针（用于链表）
}

FixedSizePool::~FixedSizePool() {
    // 释放所有块
    for (void* chunk : chunks_) {
        ::operator delete(chunk);
    }
}

void* FixedSizePool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 如果空闲链表为空，分配新块
    if (!free_list_) {
        allocate_chunk();
    }
    
    // 从空闲链表取出一个块
    Block* block = free_list_;
    free_list_ = free_list_->next;
    
    stats_.total_allocated += block_size_;
    
    return block;
}

void FixedSizePool::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 将块放回空闲链表
    Block* block = static_cast<Block*>(ptr);
    block->next = free_list_;
    free_list_ = block;
    
    stats_.total_freed += block_size_;
}

PoolStats FixedSizePool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void FixedSizePool::allocate_chunk() {
    // 分配一块大内存
    const size_t chunk_memory_size = block_size_ * chunk_size_;
    void* chunk = ::operator new(chunk_memory_size);
    chunks_.push_back(chunk);
    
    // 将块加入空闲链表
    char* ptr = static_cast<char*>(chunk);
    for (size_t i = 0; i < chunk_size_; ++i) {
        Block* block = reinterpret_cast<Block*>(ptr + i * block_size_);
        block->next = free_list_;
        free_list_ = block;
    }
    
    stats_.pool_size += chunk_memory_size;
    stats_.chunk_count++;
}

// ==================== MemoryPool ====================

MemoryPool::MemoryPool(const PoolConfig& config)
    : config_(config) {
    // 创建不同大小的池
    // 小块池：16, 32, 64 字节
    pools_.emplace_back(16, config.chunk_size);
    pools_.emplace_back(32, config.chunk_size);
    pools_.emplace_back(64, config.chunk_size);
    
    // 中块池：128, 256, 512, 1024 字节
    pools_.emplace_back(128, config.chunk_size);
    pools_.emplace_back(256, config.chunk_size);
    pools_.emplace_back(512, config.chunk_size);
    pools_.emplace_back(1024, config.chunk_size);
    
    // 大块池：2048, 4096, 8192, 16384, 32768, 65536 字节
    pools_.emplace_back(2048, config.chunk_size);
    pools_.emplace_back(4096, config.chunk_size);
    pools_.emplace_back(8192, config.chunk_size);
    pools_.emplace_back(16384, config.chunk_size);
    pools_.emplace_back(32768, config.chunk_size);
    pools_.emplace_back(65536, config.chunk_size);
}

void* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    
    total_allocated_.fetch_add(size, std::memory_order_relaxed);
    
    // 大于最大块大小，直接用系统分配
    if (size > config_.large_block_size) {
        return ::operator new(size);
    }
    
    // 选择合适的池
    const size_t index = pool_index(size);
    if (index < pools_.size()) {
        return pools_[index].allocate();
    }
    
    // 兜底：直接分配
    return ::operator new(size);
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return;
    }
    
    total_freed_.fetch_add(size, std::memory_order_relaxed);
    
    // 大于最大块大小，直接用系统释放
    if (size > config_.large_block_size) {
        ::operator delete(ptr);
        return;
    }
    
    // 选择合适的池
    const size_t index = pool_index(size);
    if (index < pools_.size()) {
        pools_[index].deallocate(ptr);
        return;
    }
    
    // 兜底：直接释放
    ::operator delete(ptr);
}

PoolStats MemoryPool::stats() const {
    PoolStats total;
    for (const auto& pool : pools_) {
        auto s = pool.stats();
        total.total_allocated += s.total_allocated;
        total.total_freed += s.total_freed;
        total.pool_size += s.pool_size;
        total.chunk_count += s.chunk_count;
    }
    total.total_allocated += total_allocated_.load(std::memory_order_relaxed);
    total.total_freed += total_freed_.load(std::memory_order_relaxed);
    return total;
}

void MemoryPool::reset() {
    for (auto& pool : pools_) {
        // FixedSizePool 没有重置功能，需要重新创建
        // 这里简单处理，实际可以添加 reset 方法
    }
}

size_t MemoryPool::pool_index(size_t size) {
    // 根据大小选择池索引
    // 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    if (size <= 2048) return 7;
    if (size <= 4096) return 8;
    if (size <= 8192) return 9;
    if (size <= 16384) return 10;
    if (size <= 32768) return 11;
    if (size <= 65536) return 12;
    return 13; // 超出范围
}

// ==================== Arena ====================

Arena::Arena(size_t block_size, bool thread_safe)
    : block_size_(block_size), thread_safe_(thread_safe) {
}

Arena::~Arena() {
    reset();
}

void* Arena::allocate(size_t size) {
    // 对齐到 8 字节
    size = (size + 7) & ~7;

    auto do_alloc = [&] {
        // 如果当前块空间不足，分配新块
        if (!current_block_ || current_offset_ + size > block_size_) {
            allocate_block();
        }

        // 从当前块分配
        void* ptr = current_block_ + current_offset_;
        current_offset_ += size;

        stats_.total_allocated += size;
        return ptr;
    };

    if (thread_safe_) {
        std::lock_guard lock(mutex_);
        return do_alloc();
    }
    return do_alloc();
}

void Arena::reset() {
    auto do_reset = [&] {
        for (void* block : blocks_) {
            ::operator delete(block);
        }
        blocks_.clear();
        current_block_ = nullptr;
        current_offset_ = 0;
        stats_ = PoolStats{};
    };

    if (thread_safe_) {
        std::lock_guard lock(mutex_);
        do_reset();
    } else {
        do_reset();
    }
}

PoolStats Arena::stats() const {
    if (thread_safe_) {
        std::lock_guard lock(mutex_);
        return stats_;
    }
    return stats_;
}

void Arena::allocate_block() {
    void* block = ::operator new(block_size_);
    blocks_.push_back(block);
    current_block_ = static_cast<char*>(block);
    current_offset_ = 0;

    stats_.pool_size += block_size_;
    stats_.chunk_count++;
}

}  // namespace ben_gear::base::memory
