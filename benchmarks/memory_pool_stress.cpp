
#include "ben_gear/base/memory/pool.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/container/string.hpp"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

using namespace ben_gear::base;

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }
private:
    std::chrono::high_resolution_clock::time_point start_;
};

// ============ 单线程混合大小分配/释放 ============
void test_mixed_sizes() {
    std::cout << "\n=== Mixed Size Alloc/Free (100k ops, 4~1024 bytes) ===\n";

    const size_t iterations = 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> size_dist(4, 1024);

    // 系统分配
    {
        Timer timer;
        std::vector<std::pair<void*, size_t>> ptrs;
        ptrs.reserve(iterations);
        for (size_t i = 0; i < iterations; ++i) {
            size_t sz = size_dist(rng);
            ptrs.emplace_back(::operator new(sz), sz);
        }
        for (auto& [p, sz] : ptrs) ::operator delete(p);
        std::cout << "System alloc:     " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";
    }

    // MemoryPool
    {
        Timer timer;
        memory::MemoryPool pool;
        std::vector<std::pair<void*, size_t>> ptrs;
        ptrs.reserve(iterations);
        for (size_t i = 0; i < iterations; ++i) {
            size_t sz = size_dist(rng);
            ptrs.emplace_back(pool.allocate(sz), sz);
        }
        for (auto& [p, sz] : ptrs) pool.deallocate(p, sz);
        std::cout << "MemoryPool:       " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";
    }

    // Arena 批量分配
    {
        Timer timer;
        memory::Arena arena(4096 * 64, false);
        for (size_t i = 0; i < iterations; ++i) {
            size_t sz = size_dist(rng);
            arena.allocate(sz);
        }
        std::cout << "Arena (no free):  " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";
    }
}

// ============ 多线程并发分配/释放（FixedSizePool 核心场景） ============
void test_multithread_contention() {
    std::cout << "\n=== Multi-thread Contention (4 threads, 50k each, 64B) ===\n";

    constexpr int num_threads = 4;
    constexpr size_t per_thread = 50000;

    // 系统分配（全局锁保护 malloc）
    {
        std::atomic<size_t> counter{0};
        Timer timer;
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&] {
                std::vector<void*> ptrs;
                ptrs.reserve(per_thread);
                for (size_t i = 0; i < per_thread; ++i) {
                    ptrs.push_back(::operator new(64));
                }
                for (void* p : ptrs) ::operator delete(p);
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for (auto& t : threads) t.join();
        std::cout << "System alloc:     " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";
    }

    // FixedSizePool 三层架构
    {
        memory::FixedSizePool pool(64);
        std::atomic<size_t> counter{0};
        Timer timer;
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&] {
                std::vector<void*> ptrs;
                ptrs.reserve(per_thread);
                for (size_t i = 0; i < per_thread; ++i) {
                    ptrs.push_back(pool.allocate());
                }
                for (void* p : ptrs) pool.deallocate(p);
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for (auto& t : threads) t.join();
        std::cout << "FixedSizePool:    " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";

        auto st = pool.stats();
        std::cout << "  pool_size=" << st.pool_size << " chunks=" << st.chunk_count
                  << " alloc=" << st.total_allocated << " freed=" << st.total_freed << "\n";
    }
}

// ============ 生产-消费模型（跨线程 alloc/free） ============
void test_producer_consumer() {
    std::cout << "\n=== Producer-Consumer (cross-thread alloc/free, 200k ops) ===\n";

    constexpr size_t total_ops = 200000;

    // 系统分配
    {
        Timer timer;
        std::vector<void*> queue;
        std::mutex mtx;
        std::atomic<bool> done{false};

        std::thread producer([&] {
            for (size_t i = 0; i < total_ops; ++i) {
                void* p = ::operator new(64);
                std::lock_guard lock(mtx);
                queue.push_back(p);
            }
            done.store(true);
        });

        std::thread consumer([&] {
            size_t freed = 0;
            while (freed < total_ops) {
                std::lock_guard lock(mtx);
                if (!queue.empty()) {
                    void* p = queue.back();
                    queue.pop_back();
                    ::operator delete(p);
                    freed++;
                }
            }
        });

        producer.join();
        consumer.join();
        std::cout << "System alloc:     " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";
    }

    // FixedSizePool（线程A分配 → 线程B释放，测试 CAS 正确性）
    {
        memory::FixedSizePool pool(64);
        Timer timer;
        std::vector<void*> queue;
        std::mutex mtx;
        std::atomic<bool> done{false};

        std::thread producer([&] {
            for (size_t i = 0; i < total_ops; ++i) {
                void* p = pool.allocate();
                std::lock_guard lock(mtx);
                queue.push_back(p);
            }
            done.store(true);
        });

        std::thread consumer([&] {
            size_t freed = 0;
            while (freed < total_ops) {
                std::lock_guard lock(mtx);
                if (!queue.empty()) {
                    void* p = queue.back();
                    queue.pop_back();
                    pool.deallocate(p);
                    freed++;
                }
            }
        });

        producer.join();
        consumer.join();
        std::cout << "FixedSizePool:    " << std::fixed << std::setprecision(2)
                  << timer.elapsed_ms() << " ms\n";

        auto st = pool.stats();
        std::cout << "  alloc=" << st.total_allocated << " freed=" << st.total_freed
                  << " (balance=" << (int64_t)(st.total_allocated - st.total_freed) << ")\n";
    }
}

// ============ 多池并发（MemoryPool 多线程） ============
void test_multipool_concurrent() {
    std::cout << "\n=== Multi-Pool Concurrent (8 threads, 25k ops, mixed sizes) ===\n";

    constexpr int num_threads = 8;
    constexpr size_t per_thread = 25000;

    memory::MemoryPool pool;
    std::atomic<size_t> counter{0};

    Timer timer;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, seed = t] {
            std::mt19937 rng(seed);
            std::uniform_int_distribution<size_t> size_dist(4, 2048);
            std::vector<std::pair<void*, size_t>> ptrs;
            ptrs.reserve(per_thread);

            for (size_t i = 0; i < per_thread; ++i) {
                size_t sz = size_dist(rng);
                ptrs.emplace_back(pool.allocate(sz), sz);
            }
            for (auto& [p, sz] : ptrs) pool.deallocate(p, sz);
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& t : threads) t.join();

    std::cout << "MemoryPool (8t):  " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << " ms\n";

    auto st = pool.stats();
    std::cout << "  pool_size=" << st.pool_size << " balance="
              << (int64_t)(st.total_allocated - st.total_freed) << "\n";
}

// ============ move 语义 + 析构安全 ============
void test_move_and_destroy() {
    std::cout << "\n=== Move + Destroy Safety ===\n";

    // 场景：pool1 被 move 到 pool2，pool1 已死，pool2 继续用
    memory::FixedSizePool pool1(64);

    // 先在 pool1 里分配一些
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
        ptrs.push_back(pool1.allocate());
    }

    // move 到 pool2
    memory::FixedSizePool pool2(std::move(pool1));

    // pool2 继续正常分配
    for (int i = 0; i < 1000; ++i) {
        ptrs.push_back(pool2.allocate());
    }

    // 释放所有
    for (void* p : ptrs) pool2.deallocate(p);

    auto st = pool2.stats();
    std::cout << "pool2 after move: alloc=" << st.total_allocated
              << " freed=" << st.total_freed
              << " balance=" << (int64_t)(st.total_allocated - st.total_freed) << "\n";

    if (st.total_allocated == st.total_freed) {
        std::cout << "  ✅ move + alloc/free balance OK\n";
    } else {
        std::cout << "  ❌ BALANCE MISMATCH!\n";
    }

    // reset
    pool2.reset();
    st = pool2.stats();
    std::cout << "pool2 after reset: pool_size=" << st.pool_size << "\n";
    if (st.pool_size == 0) {
        std::cout << "  ✅ reset OK\n";
    } else {
        std::cout << "  ❌ RESET FAILED!\n";
    }
}

// ============ 退化模式（超出 MAX_POOL_COUNT） ============
void test_degradation() {
    std::cout << "\n=== Degradation Mode (>128 pools) ===\n";

    // 创建超过 MAX_POOL_COUNT 的池，验证退化到系统分配不崩溃
    std::vector<memory::FixedSizePool> pools;
    for (int i = 0; i < 200; ++i) {
        pools.emplace_back(64);
    }

    // 在每个池里分配/释放
    std::vector<std::vector<void*>> all_ptrs(pools.size());
    for (size_t i = 0; i < pools.size(); ++i) {
        for (int j = 0; j < 10; ++j) {
            all_ptrs[i].push_back(pools[i].allocate());
        }
    }
    for (size_t i = 0; i < pools.size(); ++i) {
        for (void* p : all_ptrs[i]) {
            pools[i].deallocate(p);
        }
    }

    std::cout << "  ✅ 200 pools alloc/free OK (degraded to system)\n";
}

int main(int /*argc*/, char** /*argv*/) {
    

    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║   Memory Pool Stress & Safety Test           ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";

    test_mixed_sizes();
    test_multithread_contention();
    test_producer_consumer();
    test_multipool_concurrent();
    test_move_and_destroy();
    test_degradation();

    std::cout << "\n✅ All stress tests completed!\n";
    return 0;
}
