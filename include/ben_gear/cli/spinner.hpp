#pragma once

#include "ben_gear/cli/theme.hpp"
#include "ben_gear/cli/terminal.hpp"
#include "ben_gear/base/container/string.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace ben_gear::cli {

namespace container = base::container;

/// Spinner 动画
///
/// 高性能设计：
/// - 后台线程刷新率 60ms（~16fps），不占 CPU
/// - 使用 ANSI 光标控制原地刷新，不产生滚动
/// - stop() 清除动画行，无缝衔接后续输出
/// - 独立于 Renderer，可单独使用
class Spinner {
public:
    Spinner(const Theme& theme, const TerminalCapabilities& cap)
        : theme_(theme), cap_(cap), running_(false), frame_(0) {}

    ~Spinner() { stop(); }

    /// 启动动画
    void start(std::string_view label) {
        stop();
        label_ = container::String(label);
        running_.store(true, std::memory_order_relaxed);
        frame_.store(0, std::memory_order_relaxed);
        thread_ = std::thread(&Spinner::run, this);
    }

    /// 更新标签
    void update(std::string_view label) {
        label_ = container::String(label);
    }

    /// 停止动画（清除动画行）
    void stop() {
        if (!running_.load(std::memory_order_relaxed)) return;
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
        // 清除 spinner 行
        if (cap_.is_tty) {
            auto clear = ansi::clear_line();
            fwrite(clear.data(), 1, clear.size(), stderr);
            fflush(stderr);
        }
    }

    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    const Theme& theme_;
    TerminalCapabilities cap_;
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<int> frame_;
    container::String label_;

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

            // 构造输出：\r + clear + spinner + label
            container::String output;
            output.reserve(label_.size() + 32);

            auto clear = ansi::clear_line();
            output.append(clear.data(), clear.size());

            // Spinner 符号
            if (cap_.unicode) {
                output.append(frames[idx], frame_size);
            } else {
                const char ascii_frames[] = {'-', '\\', '|', '/'};
                output.push_back(ascii_frames[idx % 4]);
            }
            output.push_back(' ');

            // 标签（dim 色）
            auto dim_code = ansi::dim();
            auto label_color = ansi::fg(theme_.system_info, cap_);
            if (!dim_code.empty()) output.append(dim_code.data(), dim_code.size());
            if (!label_color.empty()) output.append(label_color.data(), label_color.size());
            output.append(label_.data(), label_.size());
            auto reset_code = ansi::reset();
            if (!reset_code.empty()) output.append(reset_code.data(), reset_code.size());

            fwrite(output.data(), 1, output.size(), stderr);
            fflush(stderr);

            frame_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    }
};

}  // namespace ben_gear::cli
