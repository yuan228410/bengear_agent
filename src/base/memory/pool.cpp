#include "ben_gear/base/memory/pool.hpp"
#include <algorithm>
#include <bit>

namespace ben_gear::base::memory {

// ==================== FixedSizePool ====================

FixedSizePool::FixedSizePool(size_t block_size, size_t chunk_size, bool thread_safe)
    : block_size_(std::max(block_size, sizeof(void*)))
    , chunk_size_(chunk_size)
    , thread_safe_(thread_safe) {
}

FixedSizePool::~FixedSizePool() {
    for (size_t i = 0; i < kShardCount; ++i) {
        for (void* chunk : shards_[i].chunks) {
            ::operator delete(chunk);
        }
    }
    // 释放溢出链表中的块（它们属于某个 shard 的 chunk，已被上面的循环释放）
    // 所以这里不需要再释放溢出链表中的块本身
}

FixedSizePool::FixedSizePool(FixedSizePool&& other) noexcept
    : block_size_(other.block_size_)
    , chunk_size_(other.chunk_size_)
    , thread_safe_(other.thread_safe_)
    , overflow_list_(other.overflow_list_)
    , overflow_count_(other.overflow_count_) {
    for (size_t i = 0; i < kShardCount; ++i) {
        shards_[i].free_list = other.shards_[i].free_list;
        shards_[i].free_count = other.shards_[i].free_count;
        shards_[i].chunks = std::move(other.shards_[i].chunks);
        shards_[i].allocated = other.shards_[i].allocated;
        shards_[i].freed = other.shards_[i].freed;
        shards_[i].pool_size = other.shards_[i].pool_size;
        shards_[i].chunk_count = other.shards_[i].chunk_count;
        other.shards_[i].free_list = nullptr;
        other.shards_[i].free_count = 0;
        other.shards_[i].allocated = 0;
        other.shards_[i].freed = 0;
        other.shards_[i].pool_size = 0;
        other.shards_[i].chunk_count = 0;
    }
    other.overflow_list_ = nullptr;
    other.overflow_count_ = 0;
}

FixedSizePool& FixedSizePool::operator=(FixedSizePool&& other) noexcept {
    if (this != &other) {
        for (size_t i = 0; i < kShardCount; ++i) {
            for (void* chunk : shards_[i].chunks) {
                ::operator delete(chunk);
            }
        }
        block_size_ = other.block_size_;
        chunk_size_ = other.chunk_size_;
        thread_safe_ = other.thread_safe_;
        overflow_list_ = other.overflow_list_;
        overflow_count_ = other.overflow_count_;
        for (size_t i = 0; i < kShardCount; ++i) {
            shards_[i].free_list = other.shards_[i].free_list;
            shards_[i].free_count = other.shards_[i].free_count;
            shards_[i].chunks = std::move(other.shards_[i].chunks);
            shards_[i].allocated = other.shards_[i].allocated;
            shards_[i].freed = other.shards_[i].freed;
            shards_[i].pool_size = other.shards_[i].pool_size;
            shards_[i].chunk_count = other.shards_[i].chunk_count;
            other.shards_[i].free_list = nullptr;
            other.shards_[i].free_count = 0;
            other.shards_[i].allocated = 0;
            other.shards_[i].freed = 0;
            other.shards_[i].pool_size = 0;
            other.shards_[i].chunk_count = 0;
        }
        other.overflow_list_ = nullptr;
        other.overflow_count_ = 0;
    }
    return *this;
}

void* FixedSizePool::allocate() {
    if (!thread_safe_) {
        // 单线程路径：直接操作 shard 0，无锁
        auto& shard = shards_[0];
        if (!shard.free_list) {
            allocate_chunk(0);
        }
        if (!shard.free_list) return nullptr;
        Block* block = shard.free_list;
        shard.free_list = block->next;
        shard.free_count--;
        shard.allocated += block_size_;
        return block;
    }

    const size_t si = shard_index();
    auto& shard = shards_[si];

    // 快速路径：从自己的 shard 分配
    {
        concurrency::SpinlockGuard guard(shard.lock);
        if (shard.free_list) {
            Block* block = shard.free_list;
            shard.free_list = block->next;
            shard.free_count--;
            shard.allocated += block_size_;
            return block;
        }
    }

    // 中速路径：从全局溢出链表取一批
    Block* batch = try_pop_overflow();
    if (batch) {
        // batch 是一条链表，取出第一个返回，剩余放入自己的 shard
        Block* first = batch;
        Block* rest = batch->next;
        first->next = nullptr;
        if (rest) {
            concurrency::SpinlockGuard guard(shard.lock);
            // 追加 rest 到 shard.free_list
            Block* tail = rest;
            while (tail->next) tail = tail->next;
            tail->next = shard.free_list;
            shard.free_list = rest;
            // 统计 free_count
            size_t rest_count = 1;
            for (Block* p = rest; p->next; p = p->next) rest_count++;
            shard.free_count += rest_count;
        }
        // 统计需在 shard.lock 内，这里近似统计
        shard.allocated += block_size_;
        return first;
    }

    // 慢速路径：分配新 chunk
    {
        concurrency::SpinlockGuard guard(shard.lock);
        // 双重检查：可能其他线程已经分配了
        if (!shard.free_list) {
            allocate_chunk(si);
        }
        if (!shard.free_list) return nullptr;
        Block* block = shard.free_list;
        shard.free_list = block->next;
        shard.free_count--;
        shard.allocated += block_size_;
        return block;
    }
}

void FixedSizePool::deallocate(void* ptr) {
    if (!ptr) return;

    if (!thread_safe_) {
        auto& shard = shards_[0];
        Block* block = static_cast<Block*>(ptr);
        block->next = shard.free_list;
        shard.free_list = block;
        shard.free_count++;
        shard.freed += block_size_;
        return;
    }

    const size_t si = shard_index();
    auto& shard = shards_[si];
    Block* block = static_cast<Block*>(ptr);

    {
        concurrency::SpinlockGuard guard(shard.lock);
        block->next = shard.free_list;
        shard.free_list = block;
        shard.free_count++;
        shard.freed += block_size_;

        // 空闲块过多时，迁移一部分到全局溢出链表
        if (shard.free_count > kOverflowThreshold) {
            try_push_overflow(si);
        }
    }
}

/// shard 缓存过多时，将一半空闲块迁移到全局溢出链表
/// 调用者必须持有 shards_[shard_index].lock
void FixedSizePool::try_push_overflow(size_t shard_index) {
    auto& shard = shards_[shard_index];
    if (shard.free_count <= kOverflowThreshold) return;

    // 分割链表：保留前 kOverflowThreshold/2 个，剩余迁移
    const size_t keep = kOverflowThreshold / 2;
    Block* mid = shard.free_list;
    for (size_t i = 1; i < keep && mid; ++i) {
        mid = mid->next;
    }
    if (!mid || !mid->next) return;

    Block* migrate = mid->next;
    mid->next = nullptr;
    size_t migrate_count = shard.free_count - keep;
    shard.free_count = keep;

    // 迁移到溢出链表（不持有 shard.lock 的情况下获取 overflow_lock_）
    // 注意：当前持有 shard.lock，再获取 overflow_lock_ 不会死锁
    // 因为锁顺序永远是 shard -> overflow
    {
        concurrency::SpinlockGuard guard(overflow_lock_);
        // 追加到溢出链表头部
        Block* tail = migrate;
        while (tail->next) tail = tail->next;
        tail->next = overflow_list_;
        overflow_list_ = migrate;
        overflow_count_ += migrate_count;
    }
}

/// 从全局溢出链表取一批块
/// 返回链表头，调用者负责处理
FixedSizePool::Block* FixedSizePool::try_pop_overflow() {
    concurrency::SpinlockGuard guard(overflow_lock_);
    if (!overflow_list_) return nullptr;

    // 取最多 kStealBatch 个块
    Block* batch = overflow_list_;
    Block* tail = batch;
    size_t count = 1;
    while (tail->next && count < kStealBatch) {
        tail = tail->next;
        count++;
    }
    overflow_list_ = tail->next;
    overflow_count_ -= count;
    tail->next = nullptr;
    return batch;
}

PoolStats FixedSizePool::stats() const {
    PoolStats total;
    for (size_t i = 0; i < kShardCount; ++i) {
        concurrency::SpinlockGuard guard(shards_[i].lock);
        total.total_allocated += shards_[i].allocated;
        total.total_freed += shards_[i].freed;
        total.pool_size += shards_[i].pool_size;
        total.chunk_count += shards_[i].chunk_count;
    }
    return total;
}

void FixedSizePool::reset() {
    for (size_t i = 0; i < kShardCount; ++i) {
        concurrency::SpinlockGuard guard(shards_[i].lock);
        for (void* chunk : shards_[i].chunks) {
            ::operator delete(chunk);
        }
        shards_[i].chunks.clear();
        shards_[i].free_list = nullptr;
        shards_[i].free_count = 0;
        shards_[i].allocated = 0;
        shards_[i].freed = 0;
        shards_[i].pool_size = 0;
        shards_[i].chunk_count = 0;
    }
    {
        concurrency::SpinlockGuard guard(overflow_lock_);
        overflow_list_ = nullptr;
        overflow_count_ = 0;
    }
}

void FixedSizePool::allocate_chunk(size_t shard_index) {
    // 调用者必须持有 shards_[shard_index].lock
    auto& shard = shards_[shard_index];
    const size_t chunk_memory_size = block_size_ * chunk_size_;
    void* chunk = ::operator new(chunk_memory_size);
    shard.chunks.push_back(chunk);
    shard.pool_size += chunk_memory_size;
    shard.chunk_count += 1;

    // 构建空闲链表
    char* ptr = static_cast<char*>(chunk);
    for (size_t i = 0; i < chunk_size_; ++i) {
        Block* block = reinterpret_cast<Block*>(ptr + i * block_size_);
        block->next = shard.free_list;
        shard.free_list = block;
    }
    shard.free_count += chunk_size_;
}

// ==================== MemoryPool ====================

MemoryPool::MemoryPool(const PoolConfig& config)
    : config_(config) {
    pools_.emplace_back(16, config.chunk_size, config.thread_safe);
    pools_.emplace_back(32, config.chunk_size, config.thread_safe);
    pools_.emplace_back(64, config.chunk_size, config.thread_safe);
    pools_.emplace_back(128, config.chunk_size, config.thread_safe);
    pools_.emplace_back(256, config.chunk_size, config.thread_safe);
    pools_.emplace_back(512, config.chunk_size, config.thread_safe);
    pools_.emplace_back(1024, config.chunk_size, config.thread_safe);
    pools_.emplace_back(2048, config.chunk_size, config.thread_safe);
    pools_.emplace_back(4096, config.chunk_size, config.thread_safe);
    pools_.emplace_back(8192, config.chunk_size, config.thread_safe);
    pools_.emplace_back(16384, config.chunk_size, config.thread_safe);
    pools_.emplace_back(32768, config.chunk_size, config.thread_safe);
    pools_.emplace_back(65536, config.chunk_size, config.thread_safe);
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
        total.add(pool.stats());
    }
    total.total_allocated += total_allocated_.load(std::memory_order_relaxed);
    total.total_freed += total_freed_.load(std::memory_order_relaxed);
    return total;
}

void MemoryPool::reset() {
    for (auto& pool : pools_) {
        pool.reset();
    }
    total_allocated_.store(0, std::memory_order_relaxed);
    total_freed_.store(0, std::memory_order_relaxed);
}

/// 使用位运算计算 size 所属的桶索引
/// 桶大小：16, 32, 64, 128, 256, 512, 1K, 2K, 4K, 8K, 16K, 32K, 64K
/// 规律：size <= 2^(4+index) => index
size_t MemoryPool::pool_index(size_t size) {
    if (size <= 16) return 0;
    const size_t w = std::bit_width(size - 1);
    const size_t idx = w < 5 ? 0 : (w - 4);
    return idx > 12 ? 12 : idx;
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
        stats_.total_allocated += size;
        return ptr;
    };

    if (thread_safe_) {
        concurrency::SpinlockGuard guard(lock_);
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
        concurrency::SpinlockGuard guard(lock_);
        do_reset();
    } else {
        do_reset();
    }
}

PoolStats Arena::stats() const {
    if (thread_safe_) {
        concurrency::SpinlockGuard guard(lock_);
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
    stats_.chunk_count += 1;
}

}  // namespace ben_gear::base::memory
