#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/net/http.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <atomic>

using namespace ben_gear::net;

// 计时器
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

// ── 1. EventLoop 创建开销 ──────────────────────────────────
void bench_eventloop_creation() {
    constexpr int N = 100;
    std::vector<double> times;
    times.reserve(N);

    for (int i = 0; i < N; ++i) {
        Timer t;
        EventLoop loop;
        times.push_back(t.elapsed_ms());
    }

    double total = 0, mn = 1e9, mx = 0;
    for (auto t : times) { total += t; mn = std::min(mn, t); mx = std::max(mx, t); }
    double avg = total / N;

    std::cout << "\n=== EventLoop 创建开销 (" << N << " 次) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  平均: " << avg << " ms  最小: " << mn << " ms  最大: " << mx << " ms\n";
}

// ── 2. IoContext 创建 + 销毁开销 ───────────────────────────
void bench_iocontext_lifecycle() {
    constexpr int N = 50;
    std::vector<double> times;
    times.reserve(N);

    for (int i = 0; i < N; ++i) {
        Timer t;
        {
            IoContext ctx("bench");
            // 确保 EventLoop 线程已启动并运行
            ctx.submit_task([](){});
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        times.push_back(t.elapsed_ms());
    }

    double total = 0, mn = 1e9, mx = 0;
    for (auto t : times) { total += t; mn = std::min(mn, t); mx = std::max(mx, t); }
    double avg = total / N;

    std::cout << "\n=== IoContext 生命周期开销 (" << N << " 次) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  平均: " << avg << " ms  最小: " << mn << " ms  最大: " << mx << " ms\n";
}

// ── 3. submit_task 吞吐量 ──────────────────────────────────
void bench_submit_task_throughput() {
    IoContext ctx("bench");
    constexpr int N = 10000;
    std::atomic<int> counter{0};

    Timer t;
    for (int i = 0; i < N; ++i) {
        ctx.submit_task([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 等待所有任务完成
    ctx.drain();
    double elapsed = t.elapsed_ms();

    std::cout << "\n=== submit_task 吞吐量 (" << N << " 个任务) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  总耗时: " << elapsed << " ms\n";
    std::cout << "  吞吐: " << (N / elapsed * 1000) << " ops/s\n";
    std::cout << "  完成: " << counter.load() << "/" << N << "\n";
}

// ── 4. sync_wait 延迟 ──────────────────────────────────────
void bench_sync_wait_latency() {
    IoContext ctx("bench");
    constexpr int N = 100;
    std::vector<double> times;
    times.reserve(N);

    for (int i = 0; i < N; ++i) {
        Timer t;
        auto result = sync_wait(ctx.loop(), []() -> Task<int> {
            co_return 42;
        }());
        times.push_back(t.elapsed_ms());
        (void)result;
    }

    double total = 0, mn = 1e9, mx = 0;
    for (auto t : times) { total += t; mn = std::min(mn, t); mx = std::max(mx, t); }
    double avg = total / N;

    std::cout << "\n=== sync_wait 延迟 (" << N << " 次简单协程) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  平均: " << avg << " ms  最小: " << mn << " ms  最大: " << mx << " ms\n";
    std::cout << "  P99: " << times[size_t(N * 0.99)] << " ms\n";
}

// ── 5. 定时器精度 ──────────────────────────────────────────
void bench_timer_precision() {
    IoContext ctx("bench");
    constexpr int N = 50;
    constexpr int delay_ms = 10;
    std::vector<double> errors;
    errors.reserve(N);

    for (int i = 0; i < N; ++i) {
        auto start = std::chrono::steady_clock::now();
        sync_wait(ctx.loop(), [&]() -> Task<void> {
            co_await ctx.loop().sleep_for(std::chrono::milliseconds(delay_ms));
        }());
        auto end = std::chrono::steady_clock::now();
        auto actual_ms = std::chrono::duration<double, std::milli>(end - start).count();
        errors.push_back(actual_ms - delay_ms);
    }

    double total_err = 0;
    for (auto e : errors) total_err += e;
    double avg_err = total_err / N;

    std::cout << "\n=== 定时器精度 (目标 " << delay_ms << "ms, " << N << " 次) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  平均误差: " << avg_err << " ms\n";
    std::cout << "  最大误差: " << *std::max_element(errors.begin(), errors.end()) << " ms\n";
}

// ── 6. wakeup 通知延迟 ──────────────────────────────────────
void bench_wakeup_latency() {
    IoContext ctx("bench");
    constexpr int N = 1000;
    std::atomic<int> counter{0};

    // 先预热
    for (int i = 0; i < 10; ++i) {
        ctx.submit_task([&counter](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 测量从主线程 submit 到 EventLoop 线程执行的延迟
    std::vector<double> latencies;
    latencies.reserve(N);

    for (int i = 0; i < N; ++i) {
        auto start = std::chrono::steady_clock::now();
        std::promise<void> p;
        auto f = p.get_future();
        ctx.submit_task([&p]() { p.set_value(); });
        f.wait();
        auto end = std::chrono::steady_clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }

    std::sort(latencies.begin(), latencies.end());
    double avg = 0;
    for (auto l : latencies) avg += l;
    avg /= N;

    std::cout << "\n=== wakeup 通知延迟 (" << N << " 次) ===\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  平均: " << avg << " us\n";
    std::cout << "  P50: " << latencies[N/2] << " us\n";
    std::cout << "  P99: " << latencies[size_t(N*0.99)] << " us\n";
}


// ── 7. 多 IoContext 并发 ───────────────────────────────────
void bench_multi_iocontext_concurrent() {
    constexpr int NUM_CTX = 3;  // io / workflow / util
    constexpr int TASKS_PER_CTX = 3000;
    
    std::vector<std::unique_ptr<IoContext>> contexts;
    for (int i = 0; i < NUM_CTX; ++i) {
        contexts.push_back(std::make_unique<IoContext>(
            std::string("ctx_") + std::to_string(i)));
    }
    
    std::atomic<int> total_counter{0};
    
    Timer t;
    std::vector<std::thread> threads;
    for (int c = 0; c < NUM_CTX; ++c) {
        threads.emplace_back([&contexts, c, &total_counter]() {
            for (int i = 0; i < TASKS_PER_CTX; ++i) {
                contexts[c]->submit_task([&total_counter]() {
                    total_counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }
    for (auto& th : threads) th.join();
    
    // 等待所有完成
    for (auto& ctx : contexts) ctx->drain();
    double elapsed = t.elapsed_ms();
    
    int total_tasks = NUM_CTX * TASKS_PER_CTX;
    std::cout << "\n=== 多 IoContext 并发 (" << NUM_CTX << " 上下文 x " << TASKS_PER_CTX << " 任务) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  总耗时: " << elapsed << " ms\n";
    std::cout << "  总吞吐: " << (total_tasks / elapsed * 1000) << " ops/s\n";
    std::cout << "  完成: " << total_counter.load() << "/" << total_tasks << "\n";
}

// ── 8. sync_wait 并发吞吐 ──────────────────────────────────
void bench_sync_wait_throughput() {
    IoContext ctx("bench");
    constexpr int N = 100;
    
    // 串行：一次一个 sync_wait
    Timer t_serial;
    for (int i = 0; i < N; ++i) {
        auto result = sync_wait(ctx.loop(), []() -> Task<int> {
            co_return 42;
        }());
        (void)result;
    }
    double serial_ms = t_serial.elapsed_ms();
    
    // 并行：多个线程同时 sync_wait
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = N / THREADS;
    std::vector<double> thread_times(THREADS);
    
    Timer t_parallel;
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&ctx, &thread_times, t]() {
            Timer lt;
            for (int i = 0; i < PER_THREAD; ++i) {
                auto result = sync_wait(ctx.loop(), []() -> Task<int> {
                    co_return 42;
                }());
                (void)result;
            }
            thread_times[t] = lt.elapsed_ms();
        });
    }
    for (auto& th : threads) th.join();
    double parallel_ms = t_parallel.elapsed_ms();
    
    std::cout << "\n=== sync_wait 并发吞吐 (" << N << " 协程) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  串行 (" << N << " 次): " << serial_ms << " ms  (" << (N / serial_ms * 1000) << " ops/s)\n";
    std::cout << "  并行 (" << THREADS << " 线程 x " << PER_THREAD << "): " << parallel_ms << " ms  (" << (N / parallel_ms * 1000) << " ops/s)\n";
    std::cout << "  并行加速比: " << (serial_ms / parallel_ms) << "x\n";
}

// ── 9. drain() 超时测试 ────────────────────────────────────
void bench_drain_timeout() {
    IoContext ctx("bench");
    
    // 快速场景：无任务，drain 立即返回
    Timer t_fast;
    ctx.drain(std::chrono::milliseconds{100});
    double fast_ms = t_fast.elapsed_ms();
    
    std::cout << "\n=== drain() 超时测试 ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  空上下文 drain(100ms): " << fast_ms << " ms (应 < 110ms)\n";
}

// ── 10. EventLoop 对比：单线程 vs 多线程 ────────────────────
void bench_eventloop_scalability() {
    constexpr int TASKS = 5000;
    
    // 单 EventLoop
    {
        IoContext single("single");
        std::atomic<int> counter{0};
        Timer t;
        for (int i = 0; i < TASKS; ++i) {
            single.submit_task([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        single.drain();
        double ms = t.elapsed_ms();
        std::cout << "\n=== EventLoop 扩展性 (" << TASKS << " 任务) ===\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  单 EventLoop: " << ms << " ms  (" << (TASKS / ms * 1000) << " ops/s)\n";
    }
    
    // 多 EventLoop（3 个）
    {
        std::vector<std::unique_ptr<IoContext>> contexts;
        for (int i = 0; i < 3; ++i) {
            contexts.push_back(std::make_unique<IoContext>(std::string("multi_") + std::to_string(i)));
        }
        std::atomic<int> counter{0};
        int per_ctx = TASKS / 3;
        
        Timer t;
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < per_ctx; ++i) {
                contexts[c]->submit_task([&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        }
        for (auto& ctx : contexts) ctx->drain();
        double ms = t.elapsed_ms();
        std::cout << "  3 EventLoop: " << ms << " ms  (" << (TASKS / ms * 1000) << " ops/s)\n";
    }
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  EventLoop 性能基准测试\n";
    std::cout << "========================================\n";

    bench_eventloop_creation();
    bench_iocontext_lifecycle();
    bench_submit_task_throughput();
    bench_sync_wait_latency();
    bench_timer_precision();
    bench_wakeup_latency();
    bench_multi_iocontext_concurrent();
    bench_sync_wait_throughput();
    bench_drain_timeout();
    bench_eventloop_scalability();

    std::cout << "\n========================================\n";
    std::cout << "  测试完成\n";
    std::cout << "========================================\n";

    return 0;
}
