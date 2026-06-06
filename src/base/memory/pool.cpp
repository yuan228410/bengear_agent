#include "ben_gear/base/memory/pool.hpp"
#include <algorithm>

namespace ben_gear::base::memory {

// ==================== FixedSizePool ====================

FixedSizePool::FixedSizePool(size_t block_size, size_t chunk_size)
    : block_size_(std::max(block_size, sizeof(void*)))
    , chunk_size_(chunk_size) {
}

FixedSizePool::~FixedSizePool() {
    for (void* chunk : chunks_) {
        ::operator delete(chunk);
    }
}

FixedSizePool::FixedSizePool(FixedSizePool&& other) noexcept
    : block_size_(other.block_size_)
    , chunk_size_(other.chunk_size_)
    , free_list_(other.free_list_)
    , chunks_(std::move(other.chunks_))
    , stats_(other.stats_) {
    other.free_list_ = nullptr;
    other.chunks_.clear();
    other.stats_ = PoolStats{};
}

FixedSizePool& FixedSizePool::operator=(FixedSizePool&& other) noexcept {
    if (this != &other) {
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

void* FixedSizePool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!free_list_) {
        allocate_chunk();
    }

    Block* block = free_list_;
    if (!block) {
        return nullptr;
    }

    free_list_ = block->next;
    stats_.total_allocated.fetch_add(block_size_, std::memory_order_relaxed);
    return block;
}

void FixedSizePool::deallocate(void* ptr) {
    if (!ptr) return;

    Block* block = static_cast<Block*>(ptr);
    std::lock_guard<std::mutex> lock(mutex_);
    block->next = free_list_;
    free_list_ = block;
    stats_.total_freed.fetch_add(block_size_, std::memory_order_relaxed);
}

PoolStats FixedSizePool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void FixedSizePool::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (void* chunk : chunks_) {
        ::operator delete(chunk);
    }
    chunks_.clear();
    free_list_ = nullptr;
    stats_ = PoolStats{};
}

void FixedSizePool::allocate_chunk() {
    // 调用者必须持有 mutex_
    const size_t chunk_memory_size = block_size_ * chunk_size_;
    void* chunk = ::operator new(chunk_memory_size);
    chunks_.push_back(chunk);

    // 构建空闲链表
    char* ptr = static_cast<char*>(chunk);
    for (size_t i = 0; i < chunk_size_; ++i) {
        Block* block = reinterpret_cast<Block*>(ptr + i * block_size_);
        block->next = free_list_;
        free_list_ = block;
    }

    stats_.pool_size.fetch_add(chunk_memory_size, std::memory_order_relaxed);
    stats_.chunk_count.fetch_add(1, std::memory_order_relaxed);
}

// ==================== MemoryPool ====================

MemoryPool::MemoryPool(const PoolConfig& config)
    : config_(config) {
    pools_.emplace_back(16, config.chunk_size);
    pools_.emplace_back(32, config.chunk_size);
    pools_.emplace_back(64, config.chunk_size);
    pools_.emplace_back(128, config.chunk_size);
    pools_.emplace_back(256, config.chunk_size);
    pools_.emplace_back(512, config.chunk_size);
    pools_.emplace_back(1024, config.chunk_size);
    pools_.emplace_back(2048, config.chunk_size);
    pools_.emplace_back(4096, config.chunk_size);
    pools_.emplace_back(8192, config.chunk_size);
    pools_.emplace_back(16384, config.chunk_size);
    pools_.emplace_back(32768, config.chunk_size);
    pools_.emplace_back(65536, config.chunk_size);
}

void* MemoryPool::allocate(size_t size) {
    if (size == 0) return nullptr;
    total_allocated_.fetch_add(size, std::memory_order_relaxed);
    if (size > config_.large_block_size) {
        return ::operator new(size);
    }
    const size_t index = pool_index(size);
    if (index < pools_.size()) {
        return pools_[index].allocate();
    }
    return ::operator new(size);
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
    total_freed_.fetch_add(size, std::memory_order_relaxed);
    if (size > config_.large_block_size) {
        ::operator delete(ptr);
        return;
    }
    const size_t index = pool_index(size);
    if (index < pools_.size()) {
        pools_[index].deallocate(ptr);
        return;
    }
    ::operator delete(ptr);
}

PoolStats MemoryPool::stats() const {
    PoolStats total;
    for (const auto& pool : pools_) {
        auto s = pool.stats();
        total.total_allocated.fetch_add(s.total_allocated.load(std::memory_order_relaxed), std::memory_order_relaxed);
        total.total_freed.fetch_add(s.total_freed.load(std::memory_order_relaxed), std::memory_order_relaxed);
        total.pool_size.fetch_add(s.pool_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
        total.chunk_count.fetch_add(s.chunk_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    total.total_allocated.fetch_add(total_allocated_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    total.total_freed.fetch_add(total_freed_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return total;
}

void MemoryPool::reset() {
    for (auto& pool : pools_) {
        pool.reset();
    }
    total_allocated_.store(0, std::memory_order_relaxed);
    total_freed_.store(0, std::memory_order_relaxed);
}

size_t MemoryPool::pool_index(size_t size) {
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
    return 13;
}

// ==================== Arena ====================

Arena::Arena(size_t block_size, bool thread_safe)
    : block_size_(block_size), thread_safe_(thread_safe) {
}

Arena::~Arena() {
    reset();
}

void* Arena::allocate(size_t size) {
    size = (size + 7) & ~7;
    auto do_alloc = [&] {
        if (!current_block_ || current_offset_ + size > block_size_) {
            allocate_block();
        }
        void* ptr = current_block_ + current_offset_;
        current_offset_ += size;
        stats_.total_allocated.fetch_add(size, std::memory_order_relaxed);
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
    stats_.pool_size.fetch_add(block_size_, std::memory_order_relaxed);
    stats_.chunk_count.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace ben_gear::base::memory
