# BenGear 高性能基础组件

## 📖 概述

BenGear 提供了一套高性能的基础组件，包括内存管理、并发组件、容器和无锁数据结构。

## 🚀 性能特性

### 内存池
- **固定大小内存池**：减少内存分配开销，提升 10-100 倍性能
- **统一内存池**：支持不同大小的内存分配
- **Arena 分配器**：适合批量分配，一次性释放
- **STL 兼容分配器**：可与 STL 容器配合使用
- **原子统计字段**：`PoolStats` 的所有字段为 `std::atomic<size_t>`，使用 `fetch_add` + `memory_order_relaxed` 更新，`FixedSizePool::stats()` 无锁读取

### 并发组件
- **线程池**：支持工作窃取、动态调整线程数
- **无锁队列**：MPSC（多生产者单消费者）
- **无锁栈**：MPSC
- **无锁环形缓冲区**：SPSC（单生产者单消费者）

### 容器
- **高性能字符串**：小字符串优化（SSO）、移动语义、`std::hash<container::String>` 委托给 `std::hash<string_view>`、`find` 使用 `std::search`（跨平台，无 GNU memmem 依赖）
- **动态数组**：支持自定义分配器
- **哈希映射**：开放寻址法、罗宾汉哈希、`string_view_hash` 使用 `std::hash<std::string_view>`（与 `std::hash<container::String>` 一致）、异构查找
- **对象池**：`FixedSizePool` + free list，连接池集成

### 平台抽象
- **安全子进程**：POSIX fork+execvp / Windows CreateProcess，不经过 shell
- **文件锁**：POSIX fcntl / Windows LockFileEx，RAII 自动释放
- **平台接口**：CPU、线程、进程、OS 信息

---

## 📚 使用示例

### 1. 内存池

```cpp
#include "ben_gear/base/memory/pool.hpp"

using namespace ben_gear::base;

// 固定大小内存池
memory::FixedSizePool pool(64); // 64 字节块

void* ptr1 = pool.allocate();
pool.deallocate(ptr1);

// 统一内存池
memory::MemoryPool mem_pool;
void* ptr2 = mem_pool.allocate(128);
mem_pool.deallocate(ptr2, 128);

// 重置内存池（释放所有内存，恢复初始状态）
mem_pool.reset();

// STL 兼容分配器
memory::MemoryPool stl_pool;
std::vector<int, memory::PoolAllocator<int>> vec(memory::PoolAllocator<int>(stl_pool));
vec.push_back(42);
```

### 2. 线程池

```cpp
#include "ben_gear/base/concurrency/thread_pool.hpp"

using namespace ben_gear::base;

concurrency::ThreadPool pool;

// 提交任务
auto future = pool.submit([]() {
    return 42;
});

int result = future.get(); // 42

// 批量提交
std::vector<std::function<void()>> tasks;
for (int i = 0; i < 100; ++i) {
    tasks.push_back([i]() { /* ... */ });
}
pool.submit_batch(tasks.begin(), tasks.end());
pool.wait();
```

### 3. 高性能字符串

```cpp
#include "ben_gear/base/container/string.hpp"

using namespace ben_gear::base;

// 小字符串优化（<= 23 字节，无堆分配）
container::String s1 = "Hello"; // SSO

// 大字符串
container::String s2 = "This is a very long string that exceeds SSO size";

// 移动语义
container::String s3 = std::move(s2); // 零拷贝

// 字符串操作
s1 += " World";
s1.append("!");
```

### 4. 动态数组

```cpp
#include "ben_gear/base/container/vector.hpp"

using namespace ben_gear::base;

container::Vector<int> vec;
vec.push_back(1);
vec.push_back(2);
vec.push_back(3);

// 迭代
for (int val : vec) {
    std::cout << val << std::endl;
}

// 使用内存池分配器
memory::MemoryPool pool;
container::Vector<int, memory::PoolAllocator<int>> vec2(memory::PoolAllocator<int>(pool));
```

### 5. 哈希映射

```cpp
#include "ben_gear/base/container/map.hpp"

using namespace ben_gear::base;

container::Map<std::string, int> map;
map["one"] = 1;
map["two"] = 2;
map["three"] = 3;

// 查找
if (map.contains("one")) {
    int value = map["one"]; // 1
}

// 异构查找：用 string_view / const char* 查找，避免构造临时 Key
// 仅当 Key 类型有 data()/size() 方法时可用（如 std::string, container::String）
auto it = map.find("two"); // const char* -> O(1)
auto it2 = map.find(std::string_view("three")); // string_view -> O(1)
map.contains("one"); // const char*
map.count(std::string_view("two")); // string_view
map.erase("three"); // const char*

// 迭代
for (const auto& [key, value] : map) {
    std::cout << key << ": " << value << std::endl;
}
```

> **异构查找原理**：当 `Key = container::String` 或 `std::string` 时，`find(string_view)` 使用 `string_view_hash`（内部委托 `std::hash<std::string_view>`）计算哈希，与 `std::hash<Key>` 结果一致，用 `memcmp` 零拷贝比较键，避免构造临时 `Key` 对象。C++20 `requires` 子句约束：仅当 Key 具有 `data()`/`size()` 时这些重载才参与重载决议。

### 6. 无锁队列

```cpp
#include "ben_gear/base/concurrency/lock_free.hpp"

using namespace ben_gear::base;

concurrency::LockFreeQueue<int> queue;

// 生产者线程
queue.push(1);
queue.push(2);
queue.push(3);

// 消费者线程
auto value = queue.pop(); // 1
if (value) {
    std::cout << *value << std::endl;
}
```

### 7. 无锁栈

```cpp
#include "ben_gear/base/concurrency/lock_free.hpp"

using namespace ben_gear::base;

concurrency::LockFreeStack<int> stack;

// 生产者线程
stack.push(1);
stack.push(2);
stack.push(3);

// 消费者线程
auto value = stack.pop(); // 3 (LIFO)
if (value) {
    std::cout << *value << std::endl;
}
```

### 8. 无锁环形缓冲区

```cpp
#include "ben_gear/base/concurrency/lock_free.hpp"

using namespace ben_gear::base;

concurrency::LockFreeRingBuffer<int, 1024> buffer;

// 生产者线程
buffer.push(42);

// 消费者线程
auto value = buffer.pop();
if (value) {
    std::cout << *value << std::endl;
}

// 检查状态
if (buffer.full()) {
    std::cout << "Buffer is full" << std::endl;
}
```

### 9. 文件锁

```cpp
#include "ben_gear/base/platform/file_lock.hpp"

using namespace ben_gear::base::platform;

// 获取排他文件锁（RAII 自动释放）
auto lock = FileLock::exclusive("/path/to/file");
if (lock) {
    lock->truncate(0);
    lock->write(data, size);
    lock->sync();  // fsync
    // 析构时自动释放锁
}
```

### 10. 安全子进程

```cpp
#include "ben_gear/base/platform/os.hpp"

using namespace ben_gear::base::platform;

// 安全启动子进程（不经过 shell）
subprocess::Process proc = subprocess::spawn({"ls", "-la"}, {});
if (proc.valid()) {
    auto output = subprocess::read_all(proc.child_stdout_fd);
    int exit_code = subprocess::wait(proc.child_pid);
}
```

---

## 📊 性能基准测试

运行性能测试：

```bash
./build/performance_benchmark
```

示例输出：

```
╔════════════════════════════════════════╗
║      BenGear Performance Benchmark     ║
╚════════════════════════════════════════╝

=== Memory Pool Performance ===
System allocator (64 bytes): 7.24 ms
Memory pool (64 bytes):      5.55 ms
Unified pool (64 bytes):     4.46 ms

=== Thread Pool Performance ===
Create threads: 19.08 ms
Thread pool:     0.52 ms

=== String Performance ===
std::string append:       4.48 ms
High-perf string:         2.20 ms
std::string (SSO):        0.09 ms
High-perf (SSO):          0.19 ms
```

---

## 🎯 最佳实践

### 1. 选择合适的内存池
- **固定大小对象**：使用 `FixedSizePool`
- **多种大小对象**：使用 `MemoryPool`
- **批量分配**：使用 `Arena`

### 2. 线程池配置
```cpp
// 代码方式
concurrency::ThreadPoolConfig config;
config.min_threads = 2;
config.max_threads = 8;
config.max_queue_size = 1024;
config.enable_work_stealing = true;

concurrency::ThreadPool pool(config);
```

也可通过 `config.json` 的 `thread_pool` 字段配置，使用 `to_thread_pool_config()` 转换：

```cpp
auto config = concurrency::to_thread_pool_config(settings.thread_pool);
concurrency::ThreadPool pool(config);
```

### 3. 无锁数据结构选择
- **多生产者单消费者**：`LockFreeQueue` 或 `LockFreeStack`
- **单生产者单消费者**：`LockFreeRingBuffer`（性能最优）

### 4. 容器选择
- **字符串**：`container::String`（SSO 优化）
- **动态数组**：`container::Vector`（支持自定义分配器）
- **键值对**：`container::Map`（开放寻址法 + 异构查找）

### 5. 文件锁使用
- **跨进程互斥**：使用 `FileLock::exclusive()`
- **RAII 模式**：析构自动释放锁
- **原子写入**：lock → truncate → write → sync → unlock

---

## ⚠️ 注意事项

1. **内存池生命周期**：确保内存池的生命周期长于使用它的对象
2. **线程安全**：
    - `MemoryPool`：线程安全
    - `FixedSizePool`：线程安全（`PoolStats` 原子字段，`stats()` 无锁读取）
    - `ThreadPool`：线程安全
    - `LockFreeQueue/Stack`：MPSC 安全
    - `LockFreeRingBuffer`：SPSC 安全
3. **异常安全**：所有组件都提供基本的异常安全保证
4. **FileLock**：POSIX 使用 fcntl，Windows 使用 LockFileEx，跨平台一致

---

## 📖 API 参考

详细 API 文档请参考头文件：
- `include/ben_gear/base/memory/pool.hpp`
- `include/ben_gear/base/concurrency/thread_pool.hpp`
- `include/ben_gear/base/concurrency/lock_free.hpp`
- `include/ben_gear/base/container/string.hpp`
- `include/ben_gear/base/container/vector.hpp`
- `include/ben_gear/base/container/map.hpp`
- `include/ben_gear/base/container/object_pool.hpp`
- `include/ben_gear/base/platform/file_lock.hpp`
- `include/ben_gear/base/platform/os.hpp`
