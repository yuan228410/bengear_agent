#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace ben_gear::base::concurrency {

/// 无锁队列（MPSC - 多生产者单消费者）
/// 特性：
/// - 基于链表实现
/// - push 线程安全（多生产者）
/// - pop 仅单消费者安全，多消费者需外部同步
template <typename T>
class LockFreeQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;

        Node() : next(nullptr) {}
        explicit Node(const T& value) : data(value), next(nullptr) {}
        explicit Node(T&& value) : data(std::move(value)), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;

public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        while (pop()) {}
        delete head_.load(std::memory_order_relaxed);
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    /// 推入元素（多生产者线程安全）
    void push(const T& value) {
        Node* node = new Node(value);
        push_node(node);
    }

    /// 推入元素（多生产者线程安全，移动语义）
    void push(T&& value) {
        Node* node = new Node(std::move(value));
        push_node(node);
    }

    /// 弹出元素（仅单消费者安全！多消费者需外部加锁）
    /// @return 如果队列非空，返回元素值；否则返回空
    std::optional<T> pop() {
Node* head = head_.load(std::memory_order_acquire);
 // 自旋等待 next 指针就绪：push_node 先 exchange(tail_) 再 store(prev->next)，
 // 在两者之间 pop 可能读到 next==nullptr，此时需短暂等待而非误判为空
Node* next = head->next.load(std::memory_order_acquire);

if (next == nullptr) {
 // 可能是 push 正在进行中（tail 已更新但 next 未就绪），短暂自旋
 for (int spin = 0; spin < 64; ++spin) {
 next = head->next.load(std::memory_order_acquire);
 if (next != nullptr) break;
 // CPU pause 提示，减少功耗
#if defined(_M_X86) || defined(__x86_64__)
 __builtin_ia32_pause();
#elif defined(_M_ARM) || defined(__aarch64__)
 __asm__ __volatile__("yield" ::: "memory");
#endif
 }
 if (next == nullptr) {
 return std::nullopt;
 }
}

T value = std::move(next->data);
        head_.store(next, std::memory_order_release);
        delete head;

        return value;
    }

    /// 检查队列是否为空
    bool empty() const {
        Node* head = head_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);
        return next == nullptr;
    }

private:
    void push_node(Node* node) {
        Node* prev_tail = tail_.exchange(node, std::memory_order_acq_rel);
        prev_tail->next.store(node, std::memory_order_release);
    }
};

/// 无锁栈（MPSC - 多生产者单消费者）
/// 使用带版本号的指针解决 ABA 问题
/// 跨平台实现：自旋锁保护 (ptr, tag) 对，避免依赖 __int128 / -latomic
/// 注意：内部使用 spinlock 保护 CAS，不是严格意义上的 lock-free
template <typename T>
class ConcurrentStack {
private:
    struct Node {
        T data;
        Node* next;

        explicit Node(const T& value) : data(value), next(nullptr) {}
        explicit Node(T&& value) : data(std::move(value)), next(nullptr) {}
    };

    /// 带版本号的指针，防止 ABA 问题
    struct TaggedPtr {
        Node* ptr;
        uintptr_t tag;

        TaggedPtr() : ptr(nullptr), tag(0) {}
        TaggedPtr(Node* p, uintptr_t t) : ptr(p), tag(t) {}
    };

    alignas(64) std::atomic<Node*> ptr_;
    alignas(64) std::atomic<uintptr_t> tag_;

    /// 自旋锁：用于原子地读写 (ptr, tag) 对
    std::atomic<bool> spinlock_{false};

    TaggedPtr load_head() const {
        // const 方法需 const_cast，因为 spinlock 本质上是 mutable 同步原语
        auto& lock = const_cast<std::atomic<bool>&>(spinlock_);
        while (lock.exchange(true, std::memory_order_acquire)) {}
        TaggedPtr result(ptr_.load(std::memory_order_relaxed),
                         tag_.load(std::memory_order_relaxed));
        lock.store(false, std::memory_order_release);
        return result;
    }

    bool cas_head(TaggedPtr& expected, const TaggedPtr& desired) {
        while (spinlock_.exchange(true, std::memory_order_acquire)) {}
        TaggedPtr current(ptr_.load(std::memory_order_relaxed),
                          tag_.load(std::memory_order_relaxed));
        if (current.ptr != expected.ptr || current.tag != expected.tag) {
            expected = current;
            spinlock_.store(false, std::memory_order_release);
            return false;
        }
        ptr_.store(desired.ptr, std::memory_order_relaxed);
        tag_.store(desired.tag, std::memory_order_relaxed);
        spinlock_.store(false, std::memory_order_release);
        return true;
    }

public:
    ConcurrentStack() : ptr_(nullptr), tag_(0) {}

    ~ConcurrentStack() {
        while (pop()) {}
    }

    ConcurrentStack(const ConcurrentStack&) = delete;
    ConcurrentStack& operator=(const ConcurrentStack&) = delete;

    /// 推入元素（多生产者线程安全）
    void push(const T& value) {
        Node* node = new Node(value);
        push_node(node);
    }

    /// 推入元素（多生产者线程安全，移动语义）
    void push(T&& value) {
        Node* node = new Node(std::move(value));
        push_node(node);
    }

    /// 弹出元素（仅单消费者安全，多消费者需外部加锁）
    std::optional<T> pop() {
        TaggedPtr expected = load_head();
        while (true) {
            if (expected.ptr == nullptr) {
                return std::nullopt;
            }
            TaggedPtr desired(expected.ptr->next, expected.tag + 1);
            if (cas_head(expected, desired)) {
                T value = std::move(expected.ptr->data);
                delete expected.ptr;
                return value;
            }
            // cas_head 更新 expected 为当前值，重试
        }
    }

    /// 检查栈是否为空
    bool empty() const {
        TaggedPtr head = load_head();
        return head.ptr == nullptr;
    }

private:
    void push_node(Node* node) {
        TaggedPtr expected = load_head();
        while (true) {
            node->next = expected.ptr;
            TaggedPtr desired(node, expected.tag + 1);
            if (cas_head(expected, desired)) {
                return;
            }
        }
    }
};

/// 无锁环形缓冲区（SPSC - 单生产者单消费者）
/// 特性：
/// - 固定大小
/// - 无锁操作
/// - 高性能
template <typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
private:
    alignas(64) std::atomic<size_t> write_index_{0};
    alignas(64) std::atomic<size_t> read_index_{0};
    alignas(64) T buffer_[Capacity];
    
public:
    LockFreeRingBuffer() = default;
    
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;
    
    /// 写入元素（单生产者调用）
    /// @return 如果成功写入返回 true，否则返回 false（缓冲区已满）
    bool push(const T& value) {
        const size_t write = write_index_.load(std::memory_order_relaxed);
        const size_t read = read_index_.load(std::memory_order_acquire);
        
        if (write - read >= Capacity) {
            return false;  // 缓冲区已满
        }
        
        buffer_[write & (Capacity - 1)] = value;
        write_index_.store(write + 1, std::memory_order_release);
        return true;
    }
    
    /// 写入元素（单生产者调用，移动语义）
    bool push(T&& value) {
        const size_t write = write_index_.load(std::memory_order_relaxed);
        const size_t read = read_index_.load(std::memory_order_acquire);
        
        if (write - read >= Capacity) {
            return false;  // 缓冲区已满
        }
        
        buffer_[write & (Capacity - 1)] = std::move(value);
        write_index_.store(write + 1, std::memory_order_release);
        return true;
    }
    
    /// 读取元素（单消费者调用）
    /// @return 如果成功读取返回元素值，否则返回空
    std::optional<T> pop() {
        const size_t read = read_index_.load(std::memory_order_relaxed);
        const size_t write = write_index_.load(std::memory_order_acquire);
        
        if (read >= write) {
            return std::nullopt;  // 缓冲区为空
        }
        
        T value = std::move(buffer_[read & (Capacity - 1)]);
        read_index_.store(read + 1, std::memory_order_release);
        return value;
    }
    
    /// 检查缓冲区是否为空
    bool empty() const {
        return read_index_.load(std::memory_order_acquire) >= 
               write_index_.load(std::memory_order_acquire);
    }
    
    /// 检查缓冲区是否已满
    bool full() const {
        return write_index_.load(std::memory_order_acquire) - 
               read_index_.load(std::memory_order_acquire) >= Capacity;
    }
    
    /// 获取当前元素数量
    size_t size() const {
        return write_index_.load(std::memory_order_acquire) - 
               read_index_.load(std::memory_order_acquire);
    }
    
    /// 获取容量
    static constexpr size_t capacity() { return Capacity; }
};

/// 无锁计数器（线程安全）
/// 使用适当的内存序保证计数与关联数据的一致性
template <typename T>
class LockFreeCounter {
    static_assert(std::is_integral<T>::value, "T must be integral type");

private:
    std::atomic<T> value_;

public:
    LockFreeCounter() : value_(0) {}
    explicit LockFreeCounter(T initial) : value_(initial) {}

    /// 增加计数（acq_rel：确保之前的写对其他线程可见）
    T increment() {
        return value_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    /// 减少计数（acq_rel：确保之前的写对其他线程可见）
    T decrement() {
        return value_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    }

    /// 获取当前值（acquire：确保看到所有之前的写）
    T get() const {
        return value_.load(std::memory_order_acquire);
    }

    /// 设置值（release：确保之前的写不会被重排到此之后）
    void set(T value) {
        value_.store(value, std::memory_order_release);
    }

    /// 重置为 0
    void reset() {
        value_.store(0, std::memory_order_release);
    }

    /// 比较并交换（acq_rel）
    bool compare_exchange(T& expected, T desired) {
        return value_.compare_exchange_weak(expected, desired,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    }
};

}  // namespace ben_gear::base::concurrency
