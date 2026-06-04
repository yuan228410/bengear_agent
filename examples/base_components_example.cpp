#include "ben_gear/base/memory/pool.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/concurrency/lock_free.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"

#include <iostream>
#include <string>

using namespace ben_gear::base;

// 示例 1: 内存池
void example_memory_pool() {
    std::cout << "\n=== Memory Pool Example ===\n";
    
    // 固定大小内存池
    memory::FixedSizePool pool(64);
    void* ptr1 = pool.allocate();
    void* ptr2 = pool.allocate();
    
    std::cout << "Allocated from fixed pool: " << ptr1 << ", " << ptr2 << "\n";
    
    pool.deallocate(ptr1);
    pool.deallocate(ptr2);
    
    // 统一内存池
    memory::MemoryPool mem_pool;
    void* ptr3 = mem_pool.allocate(128);
    void* ptr4 = mem_pool.allocate(256);
    
    std::cout << "Allocated from unified pool: " << ptr3 << ", " << ptr4 << "\n";
    
    mem_pool.deallocate(ptr3, 128);
    mem_pool.deallocate(ptr4, 256);
}

// 示例 2: 线程池
void example_thread_pool() {
    std::cout << "\n=== Thread Pool Example ===\n";
    
    concurrency::ThreadPool pool;
    
    // 提交任务
    auto future1 = pool.submit([]() {
        std::cout << "Task 1 executed\n";
        return 42;
    });
    
    auto future2 = pool.submit([]() {
        std::cout << "Task 2 executed\n";
        return 100;
    });
    
    int result1 = future1.get();
    int result2 = future2.get();
    
    std::cout << "Results: " << result1 << ", " << result2 << "\n";
}

// 示例 3: 高性能字符串
void example_string() {
    std::cout << "\n=== String Example ===\n";
    
    // 小字符串优化
    container::String s1(container::String("Hello"));
    std::cout << "Small string: " << s1.c_str() << " (size: " << s1.size() << ")\n";
    
    // 大字符串
    container::String s2(container::String("This is a very long string that exceeds SSO size"));
    std::cout << "Large string: " << s2.c_str() << " (size: " << s2.size() << ")\n";
    
    // 字符串操作
    s1 += " World";
    s1.append("!");
    std::cout << "Concatenated: " << s1.c_str() << "\n";
}

// 示例 4: 动态数组
void example_vector() {
    std::cout << "\n=== Vector Example ===\n";
    
    container::Vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    
    std::cout << "Vector elements: ";
    for (int val : vec) {
        std::cout << val << " ";
    }
    std::cout << "\n";
    
    std::cout << "Size: " << vec.size() << ", Capacity: " << vec.capacity() << "\n";
}

// 示例 5: 哈希映射
void example_map() {
    std::cout << "\n=== Map Example ===\n";
    
    container::Map<std::string, int> map;
    map["one"] = 1;
    map["two"] = 2;
    map["three"] = 3;
    
    std::cout << "Map elements:\n";
    for (const auto& [key, value] : map) {
        std::cout << "  " << key << ": " << value << "\n";
    }
    
    std::cout << "Contains 'two': " << (map.contains("two") ? "yes" : "no") << "\n";
}

// 示例 6: 无锁队列
void example_lock_free_queue() {
    std::cout << "\n=== Lock-Free Queue Example ===\n";
    
    concurrency::LockFreeQueue<int> queue;
    
    // 生产者
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    // 消费者
    std::cout << "Queue elements: ";
    while (auto value = queue.pop()) {
        std::cout << *value << " ";
    }
    std::cout << "\n";
}

// 示例 7: 无锁栈
void example_lock_free_stack() {
    std::cout << "\n=== Lock-Free Stack Example ===\n";
    
    concurrency::LockFreeStack<int> stack;
    
    // 生产者
    stack.push(1);
    stack.push(2);
    stack.push(3);
    
    // 消费者
    std::cout << "Stack elements (LIFO): ";
    while (auto value = stack.pop()) {
        std::cout << *value << " ";
    }
    std::cout << "\n";
}

// 示例 8: 无锁环形缓冲区
void example_ring_buffer() {
    std::cout << "\n=== Ring Buffer Example ===\n";
    
    concurrency::LockFreeRingBuffer<int, 8> buffer;
    
    // 生产者
    for (int i = 1; i <= 5; ++i) {
        buffer.push(i);
    }
    
    std::cout << "Buffer size: " << buffer.size() << "/" << buffer.capacity() << "\n";
    
    // 消费者
    std::cout << "Buffer elements: ";
    while (auto value = buffer.pop()) {
        std::cout << *value << " ";
    }
    std::cout << "\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   BenGear Base Components Examples     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    example_memory_pool();
    example_thread_pool();
    example_string();
    example_vector();
    example_map();
    example_lock_free_queue();
    example_lock_free_stack();
    example_ring_buffer();
    
    std::cout << "\n✅ All examples completed!\n";
    
    return 0;
}
