#pragma once

#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace ben_gear::cli {


/// Spinner 动画
///
/// 高性能设计：
/// - 后台线程刷新率 60ms（~16fps），不占 CPU
/// - 使用 ANSI 光标控制原地刷新，不产生滚动
/// - stop() 清除动画行，无缝衔接后续输出
/// - 独立于 Renderer，可单独使用
class Spinner {
public:
    Spinner(const Theme& theme, const TerminalCapabilities& cap,
            const AnsiStyleCache& cache)
        : theme_(theme), cap_(cap), cache_(cache), running_(false), frame_(0) {}

    ~Spinner() { stop(); }

    /// 启动动画
    void start(std::string_view label) {
        stop();
        {
            std::lock_guard lock(label_mutex_);
            label_ = std::string(label);
        }
        running_.store(true, std::memory_order_relaxed);
        frame_.store(0, std::memory_order_relaxed);
        thread_ = std::thread(&Spinner::run, this);
    }

    /// 更新标签
    void update(std::string_view label) {
        std::lock_guard lock(label_mutex_);
        label_ = std::string(label);
    }

    /// 停止动画（清除动画行）
    void stop() {
        if (!running_.load(std::memory_order_relaxed)) return;
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
        // 清除 spinner 行（使用预缓存 ANSI 码）
        if (cap_.is_tty) {
            auto& cl = cache_.clear_line;
            fwrite(cl.data(), 1, cl.size(), stderr);
            fflush(stderr);
        }
    }

    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    const Theme& theme_;
    TerminalCapabilities cap_;
    const AnsiStyleCache& cache_;
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<int> frame_;
    std::mutex label_mutex_;
    std::string label_;

    void run() {
        // Braille 动画帧（UTF-8 编码）
        static const char* frames[] = {
            "\xe2\xa0\x8b",  // ⠋
            "\xe2\xa0\x99",  // ⠙
            "\xe2\xa0\xb9",  // ⠹
            "\xe2\xa0\xb8",  // ⠸
            "\xe2\xa0\xbc",  // ⠼
            "\xe2\xa0\xb4",  // ⠴
            "\xe2\xa0\xa6",  // ⠦
            "\xe2\xa0\xa7",  // ⠧
            "\xe2\xa0\x87",  // ⠇
            "\xe2\xa0\x8f",  // ⠏
        };
        static constexpr int frame_count = 10;
        static constexpr int frame_size = 3;  // UTF-8 字节数

        while (running_.load(std::memory_order_relaxed)) {
            int idx = frame_.load(std::memory_order_relaxed) % frame_count;

            // 栈缓冲 + 预缓存 ANSI 码，零堆分配
            char buf[256];
            int pos = 0;

            // \r + clear line
            auto& cl = cache_.clear_line;
            std::memcpy(buf + pos, cl.data(), cl.size()); pos += static_cast<int>(cl.size());

            // Spinner 符号
            if (cap_.unicode) {
                std::memcpy(buf + pos, frames[idx], frame_size); pos += frame_size;
            } else {
                const char ascii_frames[] = {'-', '\\', '|', '/'};
                buf[pos++] = ascii_frames[idx % 4];
            }
            buf[pos++] = ' ';

            // 标签（dim + system_info 色）
            {
                std::lock_guard lock(label_mutex_);
                auto& dim_c = cache_.dim;
                if (!dim_c.empty()) { std::memcpy(buf + pos, dim_c.data(), dim_c.size()); pos += static_cast<int>(dim_c.size()); }
                auto& sys_c = cache_.system_info;
                if (!sys_c.empty()) { std::memcpy(buf + pos, sys_c.data(), sys_c.size()); pos += static_cast<int>(sys_c.size()); }
                std::memcpy(buf + pos, label_.data(), label_.size()); pos += static_cast<int>(label_.size());
                auto& rst = cache_.reset;
                if (!rst.empty()) { std::memcpy(buf + pos, rst.data(), rst.size()); pos += static_cast<int>(rst.size()); }
            }

            fwrite(buf, 1, static_cast<size_t>(pos), stderr);
            fflush(stderr);

            frame_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    }
};

}  // namespace ben_gear::cli
