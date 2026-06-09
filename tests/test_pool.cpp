#include <gtest/gtest.h>
#include "ben_gear/base/memory/pool.hpp"

#include <thread>
#include <vector>
#include <algorithm>

using namespace ben_gear::base::memory;

// --- PoolStats ---

TEST(PoolStats, DefaultZero) {
    PoolStats stats;
    EXPECT_EQ(stats.total_allocated, 0u);
    EXPECT_EQ(stats.total_freed, 0u);
    EXPECT_EQ(stats.pool_size, 0u);
    EXPECT_EQ(stats.chunk_count, 0u);
}

TEST(PoolStats, CopyConstructor) {
    PoolStats a;
    a.total_allocated = 100;
    a.total_freed = 30;
    a.pool_size = 70;
    a.chunk_count = 5;

    PoolStats b(a);
    EXPECT_EQ(b.total_allocated, 100u);
    EXPECT_EQ(b.total_freed, 30u);
    EXPECT_EQ(b.pool_size, 70u);
    EXPECT_EQ(b.chunk_count, 5u);
}

TEST(PoolStats, Assignment) {
    PoolStats a;
    a.total_allocated = 200;
    a.total_freed = 50;

    PoolStats b;
    b = a;
    EXPECT_EQ(b.total_allocated, 200u);
    EXPECT_EQ(b.total_freed, 50u);
}

TEST(PoolStats, SelfAssignment) {
    PoolStats a;
    a.total_allocated = 42;
    a = a;  // NOLINT
    EXPECT_EQ(a.total_allocated, 42u);
}

// --- FixedSizePool ---

TEST(FixedSizePool, AllocateReturnsNonNull) {
    FixedSizePool pool(64);
    void* ptr = pool.allocate();
    EXPECT_NE(ptr, nullptr);
    pool.deallocate(ptr);
}

TEST(FixedSizePool, BlockSizeAccessor) {
    FixedSizePool pool(128);
    EXPECT_EQ(pool.block_size(), 128u);
}

TEST(FixedSizePool, DeallocateReallocReusesMemory) {
    FixedSizePool pool(64);
    void* ptr1 = pool.allocate();
    EXPECT_NE(ptr1, nullptr);
    pool.deallocate(ptr1);
    void* ptr2 = pool.allocate();
    EXPECT_NE(ptr2, nullptr);
    pool.deallocate(ptr2);
}

TEST(FixedSizePool, MultipleAllocations) {
    FixedSizePool pool(64, 4);
    std::vector<void*> ptrs;
    for (int i = 0; i < 20; ++i) {
        ptrs.push_back(pool.allocate());
    }
    for (void* p : ptrs) {
        EXPECT_NE(p, nullptr);
    }
    for (void* p : ptrs) {
        pool.deallocate(p);
    }
}

TEST(FixedSizePool, StatsTracking) {
    FixedSizePool pool(64, 4);
    void* p1 = pool.allocate();
    void* p2 = pool.allocate();

    auto stats = pool.stats();
    EXPECT_GT(stats.total_allocated, 0u);
    EXPECT_GT(stats.pool_size, 0u);
    EXPECT_GT(stats.chunk_count, 0u);

    pool.deallocate(p1);
    pool.deallocate(p2);
    auto stats2 = pool.stats();
    EXPECT_GT(stats2.total_freed, 0u);
}

TEST(FixedSizePool, Reset) {
    FixedSizePool pool(64, 4);
    pool.allocate();
    pool.allocate();

    pool.reset();
    auto stats = pool.stats();
    EXPECT_EQ(stats.pool_size, 0u);
    EXPECT_EQ(stats.chunk_count, 0u);
}

TEST(FixedSizePool, MoveConstruction) {
    FixedSizePool pool(64, 4);
    void* p = pool.allocate();

    FixedSizePool moved(std::move(pool));
    EXPECT_EQ(moved.block_size(), 64u);
    moved.deallocate(p);

    auto new_stats = moved.stats();
    EXPECT_GT(new_stats.chunk_count, 0u);
}

TEST(FixedSizePool, MoveAssignment) {
    FixedSizePool pool1(64, 4);
    pool1.allocate();

    FixedSizePool pool2(128, 4);
    pool2 = std::move(pool1);
    EXPECT_EQ(pool2.block_size(), 64u);
}

// --- MemoryPool ---

TEST(MemoryPool, SmallAllocation) {
    MemoryPool pool;
    void* ptr = pool.allocate(32);
    EXPECT_NE(ptr, nullptr);
    pool.deallocate(ptr, 32);
}

TEST(MemoryPool, MediumAllocation) {
    MemoryPool pool;
    void* ptr = pool.allocate(512);
    EXPECT_NE(ptr, nullptr);
    pool.deallocate(ptr, 512);
}

TEST(MemoryPool, LargeAllocation) {
    MemoryPool pool;
    void* ptr = pool.allocate(32768);
    EXPECT_NE(ptr, nullptr);
    pool.deallocate(ptr, 32768);
}

TEST(MemoryPool, MixedSizeAllocations) {
    MemoryPool pool;
    void* small = pool.allocate(32);
    void* medium = pool.allocate(512);
    void* large = pool.allocate(32768);

    EXPECT_NE(small, nullptr);
    EXPECT_NE(medium, nullptr);
    EXPECT_NE(large, nullptr);

    pool.deallocate(small, 32);
    pool.deallocate(medium, 512);
    pool.deallocate(large, 32768);
}

TEST(MemoryPool, StatsAggregation) {
    MemoryPool pool;
    pool.allocate(32);
    pool.allocate(512);
    pool.allocate(32768);

    auto stats = pool.stats();
    EXPECT_GT(stats.total_allocated, 0u);
}

TEST(MemoryPool, Reset) {
    MemoryPool pool;
    pool.allocate(32);
    pool.allocate(512);

    pool.reset();
    auto stats = pool.stats();
    EXPECT_EQ(stats.pool_size, 0u);
    EXPECT_EQ(stats.chunk_count, 0u);
}

// --- PoolAllocator ---

TEST(PoolAllocator, VectorIntegration) {
    MemoryPool pool;
    PoolAllocator<int> alloc(pool);

    std::vector<int, PoolAllocator<int>> vec(alloc);
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec[2], 3);
}

TEST(PoolAllocator, EqualitySamePool) {
    MemoryPool pool;
    PoolAllocator<int> a1(pool);
    PoolAllocator<int> a2(pool);
    EXPECT_TRUE(a1 == a2);
    EXPECT_FALSE(a1 != a2);
}

TEST(PoolAllocator, InequalityDifferentPool) {
    MemoryPool pool1, pool2;
    PoolAllocator<int> a1(pool1);
    PoolAllocator<int> a2(pool2);
    EXPECT_FALSE(a1 == a2);
    EXPECT_TRUE(a1 != a2);
}

TEST(PoolAllocator, Rebind) {
    MemoryPool pool;
    PoolAllocator<int> a(pool);
    PoolAllocator<double> b(a);
    EXPECT_TRUE(a == b);
}

// --- Arena ---

TEST(Arena, SequentialAllocation) {
    Arena arena(4096);
    void* p1 = arena.allocate(100);
    void* p2 = arena.allocate(200);
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(Arena, OverflowToNewBlock) {
    Arena arena(256);
    std::vector<void*> ptrs;
    for (int i = 0; i < 20; ++i) {
        ptrs.push_back(arena.allocate(64));
    }
    for (void* p : ptrs) {
        EXPECT_NE(p, nullptr);
    }
    auto stats = arena.stats();
    EXPECT_GT(stats.chunk_count, 1u);
}

TEST(Arena, Reset) {
    Arena arena(4096);
    arena.allocate(100);
    arena.allocate(200);

    arena.reset();
    auto stats = arena.stats();
    EXPECT_EQ(stats.pool_size, 0u);
    EXPECT_EQ(stats.chunk_count, 0u);
}

TEST(Arena, StatsTracking) {
    Arena arena(4096);
    arena.allocate(100);

    auto stats = arena.stats();
    EXPECT_GT(stats.total_allocated, 0u);
}

TEST(Arena, LargeAllocationExceedsBlockSize) {
    Arena arena(128);
    void* ptr = arena.allocate(256);
    EXPECT_NE(ptr, nullptr);
    auto stats = arena.stats();
    EXPECT_GT(stats.chunk_count, 0u);
}

// --- Concurrent tests ---

TEST(FixedSizePool, ConcurrentAllocateDeallocate) {
    FixedSizePool pool(64, 64);
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool]() {
            std::vector<void*> local_ptrs;
            for (int i = 0; i < ops_per_thread; ++i) {
                void* p = pool.allocate();
                EXPECT_NE(p, nullptr);
                local_ptrs.push_back(p);
            }
            for (void* p : local_ptrs) {
                pool.deallocate(p);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}
