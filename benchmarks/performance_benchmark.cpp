#include <glog/logging.h>

#include "ben_gear/base/memory/pool.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/container/string.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

using namespace ben_gear::base;

// 性能计时器
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

// 测试内存池性能
void test_memory_pool() {
    std::cout << "\n=== Memory Pool Performance ===\n";
    
    const size_t iterations = 100000;
    
    // 测试系统默认分配
    {
        Timer timer;
        std::vector<void*> ptrs;
        ptrs.reserve(iterations);
        
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(::operator new(64));
        }
        
        for (void* ptr : ptrs) {
            ::operator delete(ptr);
        }
        
        std::cout << "System allocator (64 bytes): " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试内存池
    {
        memory::FixedSizePool pool(64);
        Timer timer;
        std::vector<void*> ptrs;
        ptrs.reserve(iterations);
        
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(pool.allocate());
        }
        
        for (void* ptr : ptrs) {
            pool.deallocate(ptr);
        }
        
        std::cout << "Memory pool (64 bytes):      " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试统一内存池
    {
        memory::MemoryPool pool;
        Timer timer;
        std::vector<void*> ptrs;
        ptrs.reserve(iterations);
        
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(pool.allocate(64));
        }
        
        for (void* ptr : ptrs) {
            pool.deallocate(ptr, 64);
        }
        
        std::cout << "Unified pool (64 bytes):     " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
}

// 测试线程池性能
void test_thread_pool() {
    std::cout << "\n=== Thread Pool Performance ===\n";
    
    const size_t tasks = 1000;
    
    // 测试直接创建线程
    {
        Timer timer;
        std::vector<std::thread> threads;
        threads.reserve(tasks);
        
        for (size_t i = 0; i < tasks; ++i) {
            threads.emplace_back([]() {
                // 简单计算
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "Create threads:       " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试线程池
    {
        concurrency::ThreadPool pool;
        Timer timer;
        std::vector<std::future<void>> futures;
        futures.reserve(tasks);
        
        for (size_t i = 0; i < tasks; ++i) {
            futures.push_back(pool.submit([]() {
                // 简单计算
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
            }));
        }
        
        for (auto& f : futures) {
            f.get();
        }
        
        std::cout << "Thread pool:          " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
}

// 测试字符串性能
void test_string() {
    std::cout << "\n=== String Performance ===\n";
    
    const size_t iterations = 100000;
    
    // 测试 std::string
    {
        Timer timer;
        std::string result;
        
        for (size_t i = 0; i < iterations; ++i) {
            std::string s = "Hello, World! This is a test string.";
            result += s;
        }
        
        std::cout << "std::string append:   " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试高性能字符串
    {
        Timer timer;
        container::String result;
        
        for (size_t i = 0; i < iterations; ++i) {
            container::String s(container::String("Hello, World! This is a test string."));
            result += s;
        }
        
        std::cout << "High-perf string:     " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试小字符串优化
    {
        Timer timer;
        std::vector<std::string> vec;
        vec.reserve(iterations);
        
        for (size_t i = 0; i < iterations; ++i) {
            vec.push_back("short");
        }
        
        std::cout << "std::string (SSO):    " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
    
    // 测试高性能字符串小字符串优化
    {
        Timer timer;
        std::vector<container::String> vec;
        vec.reserve(iterations);
        
        for (size_t i = 0; i < iterations; ++i) {
            vec.emplace_back(container::String("short"));
        }
        
        std::cout << "High-perf (SSO):      " 
                  << std::fixed << std::setprecision(2) 
                  << timer.elapsed_ms() << " ms\n";
    }
}

int main(int /*argc*/, char** argv) {
    google::InitGoogleLogging(argv[0]);
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   BenGear Performance Benchmark        ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    test_memory_pool();
    test_thread_pool();
    test_string();
    
    std::cout << "\n✅ All benchmarks completed!\n";
    
    return 0;
}
