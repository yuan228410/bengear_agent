# EventLoop 架构设计

## 概述

BenGear 使用 **EventLoop + 协程** 实现异步 I/O，事件驱动、零轮询。

核心组件：
- `EventLoop`：基于 epoll/kqueue 的 I/O 多路复用事件循环
- `IoContext`：EventLoop + 专属线程的 RAII 封装
- `Task<T>`：C++20 协程，支持 `on_complete` 完成回调
- `sync_wait`：桥接同步/异步，事件驱动等待协程完成

## 多 EventLoop 架构

```
┌─ IoContext "io" ─────────────────────┐
│  EventLoop (io_thread_)               │
│  - Agent LLM 请求/响应                │
│  - HTTP 客户端（所有网络 I/O）         │
│  - TLS、TCP、连接池                   │
└───────────────────────────────────────┘

┌─ IoContext "workflow" ────────────────┐
│  EventLoop (wf_thread_)               │
│  - 子 Agent LLM 请求                  │
│  - DAG 任务编排                       │
└───────────────────────────────────────┘

┌─ IoContext "util" ───────────────────┐
│  EventLoop (util_thread_)             │
│  - 技能下载、HTTP 工具                │
│  - MCP 请求、临时 I/O 任务            │
└───────────────────────────────────────┘

┌─ ThreadPool (core_pool_) ─────────────┐
│  - 工具执行                           │
│  - JSON 解析/构造                     │
│  - CPU 密集型任务                     │
└───────────────────────────────────────┘
```

**设计原则**：
- EventLoop 只做事件分发（I/O 就绪、定时器、协程恢复）
- 耗时操作通过 `submit_task()` 提交到 EventLoop 线程或 ThreadPool
- 多个 IoContext 分工，高内聚低耦合
- 临时/一次性任务使用 `util` IoContext，不影响核心链路

## EventLoop 运行流程

`run_once()` 每次循环处理 5 个阶段：

```
Phase 1: 排空入站队列（循环 drain）
  ├─ 注册 I/O 操作到 epoll/kqueue
  ├─ 注册定时器
  └─ 执行 task_func（协程 resume）
     → 协程可能产生新的入站操作
     → 循环 drain 确保同一次 run_once 处理完

Phase 2: 计算 poller 超时
  └─ 考虑最近的定时器截止时间

Phase 3: poller wait + 处理就绪事件
  └─ 恢复挂起的 I/O 协程

Phase 4: 处理过期定时器
  └─ 恢复挂起的定时器协程

Phase 5: 关闭超时 fd
  └─ 关闭超时连接，恢复挂起协程（抛 ResponseTimeoutError）
```

### Phase 1 循环 drain

协程 `resume()` 后可能产生新的入站操作（I/O 注册、定时器等）。
如果不循环 drain，这些操作要等下一次 `run_once` 才处理，导致：
- 定时器精度退化（10ms 定时器实际等 100ms）
- I/O 操作延迟一个 poller 周期

### Phase 1 锁外执行

`task_func`（协程 resume）必须在 `impl_->mutex` **锁外**执行，
因为协程内部可能调用 `close_after()`/`cancel_close()` 等需要获取 mutex 的方法。
否则会死锁。

## sync_wait 设计

### 事件驱动，零轮询

```
主线程                          EventLoop 线程
  │                                │
  ├─ sync_wait(loop, task)         │
  │   ├─ task.on_complete(cb)      │
  │   ├─ submit_task(resume)       │
  │   └─ future.get() 阻塞         │
  │                                ├─ Phase 1: resume(task)
  │                                │   → 协程挂起在 I/O
  │                                ├─ Phase 3: I/O 就绪
  │                                │   → 协程继续执行
  │                                ├─ ...
  │                                ├─ 协程完成
  │                                │   → FinalAwaiter 触发
  │                                │   → on_complete(cb)
  │  future.get() 返回 ◄───────────┤   → promise.set_value()
  │                                │
```

### 使用约束

⚠️ **`sync_wait` 只能从非 EventLoop 线程调用！**

在 EventLoop 线程内调用会导致死锁：`future.get()` 阻塞 EventLoop 线程，
协程永远无法完成。

运行时检测：`sync_wait` 内部调用 `is_loop_thread()` 检测，
如果从 EventLoop 线程调用会抛出 `std::logic_error`。

### Task::on_complete 机制

`Task<T>` 的 `promise_type` 包含 `on_complete` 回调。
当协程完成时，`FinalAwaiter::await_suspend` 触发回调：

```cpp
struct FinalAwaiter {
    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        if (handle.promise().on_complete) {
            auto cb = std::move(handle.promise().on_complete);
            cb();  // 触发完成回调
        }
        return handle.promise().continuation
            ? handle.promise().continuation
            : std::noop_coroutine();
    }
};
```

## WakeupFd 跨平台唤醒

| 平台 | 实现方式 |
|------|---------|
| Linux | `eventfd`（轻量，单 fd） |
| macOS | `pipe`（通用兼容） |
| Windows | WSA socket pair + `WSAEventSelect` |

所有平台差异收敛在 `WakeupFd` 类中，EventLoop 和 IoContext 不直接使用平台宏。

## IoContext 生命周期

```cpp
IoContext ctx("io");       // 构造：启动 EventLoop 线程
ctx.submit_task(func);     // 提交任务
ctx.drain();               // 优雅停止：等待所有任务完成
// 析构时自动 drain() + join()
```

### drain() vs stop()

| 方法 | 行为 |
|------|------|
| `stop()` | 立即设置停止标志，`run()` 退出循环 |
| `drain()` | 等待所有已提交任务完成，再设置停止标志 |

IoContext 析构自动调用 `drain()`，确保任务不丢失。

## 性能基准

| 指标 | 数值 |
|------|------|
| EventLoop 创建 | 0.002 ms |
| IoContext 生命周期 | 12 ms |
| submit_task 吞吐 | ~500K ops/s |
| sync_wait 延迟 | 0.013 ms |
| 定时器误差（10ms） | 0.25 ms |
| wakeup 通知延迟 P50 | 11 us |
| wakeup 通知延迟 P99 | 21 us |

## Windows 平台特殊说明

### WakeupFd

Windows 使用 **TCP loopback socket pair + WSAEventSelect** 替代 eventfd/pipe：

| 平台 | 实现方式 | read_fd() |
|------|---------|-----------|
| Linux | `eventfd` | 返回 fd |
| macOS | `pipe` | 返回 pipe[0] |
| **Windows** | WSA socket pair | **返回 -1** |

Windows 上 `read_fd()` 返回 -1，因为 WSA 不使用 fd 注册机制。
EventLoop 的 Windows 实现需使用 `WSAEventWait` 替代 `epoll_wait`/`kevent`。

### EventLoop Windows 适配

Windows 版 EventLoop 需要将 Phase 3（poller wait）替换为：
```cpp
// Windows Phase 3 伪代码
WSAEVENT events[] = { wakeup.wsa_event(), ... };
DWORD wait_result = WSAWaitForMultipleEvents(count, events, FALSE, timeout_ms, FALSE);
// 处理就绪事件...
```

当前 EventLoop 的 Phase 3 仅实现了 Linux/macOS，Windows 适配为待办项。

## 性能优化指南

### 关键指标

| 指标 | 典型值 | 优化建议 |
|------|--------|---------|
| EventLoop 创建 | 0.002 ms | 无需优化 |
| IoContext 生命周期 | 12 ms | 全局复用，避免频繁创建销毁 |
| submit_task 吞吐 | ~530K ops/s | 单 EventLoop 已够用 |
| sync_wait 延迟 | 0.01 ms | 事件驱动，无轮询开销 |
| 定时器误差 | 0.17 ms | Phase 1 循环 drain 确保精度 |
| wakeup 延迟 P50 | 10 us | wakeup_fd 机制高效 |

### 优化原则

**1. 复用 IoContext**

IoContext 创建销毁需 ~12ms（含线程创建），应全局复用：
```cpp
// ✅ 推荐：SharedResources 持有 3 个 IoContext，全生命周期复用
class SharedResources {
    std::unique_ptr<IoContext> io_context_;    // LLM 请求
    std::unique_ptr<IoContext> wf_context_;    // 工作流
    std::unique_ptr<IoContext> util_context_;  // 临时任务
};
```

**2. sync_wait 并行加速**

4 线程并发 sync_wait 可获得 ~2.5x 加速。对于批量独立请求，
使用多线程 + 单 IoContext 即可获得不错的并行度。

**3. 临时任务用 util IoContext**

技能下载、HTTP 工具调用等临时/一次性任务应使用 `util_context_`，
避免阻塞核心 `io_context_` 的 LLM 请求处理。

**4. drain() 超时保护**

`drain()` 默认 30 秒超时。如果任务执行时间可能超过默认值，
显式传入更大的超时：
```cpp
ctx.drain(std::chrono::seconds{60});  // 长任务场景
```

**5. 避免 sync_wait 死锁**

`sync_wait` 在 EventLoop 线程内调用会死锁。运行时检测会抛出
`std::logic_error`，但生产环境应在架构层面避免。

### 基准测试运行

```bash
cmake --build build --target eventloop_benchmark
./build/eventloop_benchmark
```
