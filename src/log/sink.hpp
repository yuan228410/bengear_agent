#pragma once

#include "log/level.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ben_gear::log {

struct Record {
    Level level = Level::info;
    std::chrono::system_clock::time_point timestamp{};
    std::string message;
    uint64_t process_id = 0;   // 跨进程共享日志时区分来源
    uint64_t thread_id = 0;
    std::string trace_id;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Record& record, std::string_view formatted) = 0;
    virtual void flush() {}
};

class StdoutSink final : public Sink {
public:
    void write(const Record&, std::string_view formatted) override {
        std::lock_guard lock(mutex_);
        std::fwrite(formatted.data(), 1, formatted.size(), stdout);
        std::fwrite("\n", 1, 1, stdout);
        std::fflush(stdout);
    }

    void flush() override {
        std::lock_guard lock(mutex_);
        std::fflush(stdout);
    }

private:
    std::mutex mutex_;
};

// ==================== 文件 Sink（按天滚动 + 大小兜底） ====================
//
// 写入策略：
//   - 总是写入 path_（如 bengear.log）
//   - 跨天时：path_ → path_.YYYYMMDD.log，打开新 path_
//   - 大小超限：path_ → path_.YYYYMMDD.N.log（N=1,2,3...），打开新 path_
//   - 清理：保留最近 max_rotated_files 个轮转文件（按文件名排序）

class FileSink final : public Sink {
public:
    explicit FileSink(std::filesystem::path path,
                      int flush_interval_ms = 1000,
                      int flush_batch_size = 64,
                      size_t max_file_size = 10 * 1024 * 1024,
                      int max_rotated_files = 5)
        : path_(std::move(path)),
          flush_interval_ms_(flush_interval_ms),
          flush_batch_size_(flush_batch_size),
          max_file_size_(max_file_size),
          max_rotated_files_(max_rotated_files) {
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        open_file();
    }

    void write(const Record&, std::string_view formatted) override {
        if (!file_.is_open()) return;

        // 构造整行，防止流缓冲在行中间分裂
        // 即使缓冲模式，每次溢写都包含完整行，多进程 O_APPEND 安全
        line_buf_.clear();
        line_buf_.reserve(formatted.size() + 1);
        line_buf_.append(formatted.data(), formatted.size());
        line_buf_.push_back('\n');

        bool need_day_rotate = false;
        bool need_size_rotate = false;
        {
            std::lock_guard lock(mutex_);

            // 按天检测：节流至每秒一次，避免每条日志调 localtime
            auto now = std::chrono::steady_clock::now();
            if (now - last_day_check_ > std::chrono::seconds(1)) {
                last_day_check_ = now;
                auto today = today_str();
                if (today != file_date_) {
                    need_day_rotate = true;
                }
            }

            file_.write(line_buf_.data(), static_cast<std::streamsize>(line_buf_.size()));
            current_size_ += line_buf_.size();
            ++unflushed_;

            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now - last_flush_).count();
            if (unflushed_ >= flush_batch_size_ || elapsed_ms >= flush_interval_ms_) {
                file_.flush();
                unflushed_ = 0;
                last_flush_ = now;
            }

            need_size_rotate = (current_size_ >= max_file_size_);
        }
        if (need_day_rotate) {
            rotate_day();
        } else if (need_size_rotate) {
            rotate_size();
        }
    }

    void flush() override {
        std::lock_guard lock(mutex_);
        if (file_.is_open()) file_.flush();
        unflushed_ = 0;
        last_flush_ = std::chrono::steady_clock::now();
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    // ---- 日期工具 ----

    static std::string today_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        {
            static std::mutex tm_mutex;
            std::lock_guard lock(tm_mutex);
            tm = *std::localtime(&t);
        }
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
        return buf;
    }

    // ---- 文件操作 ----

    void open_file() {
        std::error_code ec;
        current_size_ = static_cast<size_t>(std::filesystem::file_size(path_, ec));
        file_.open(path_, std::ios::app | std::ios::out);
        file_date_ = today_str();
        day_counter_ = 0;
        last_flush_ = std::chrono::steady_clock::now();
        last_day_check_ = std::chrono::steady_clock::now();
    }

    /// 按天轮转：当前文件 → path_.YYYYMMDD.log，开新文件
    void rotate_day() {
        std::lock_guard lock(mutex_);
        auto today = today_str();
        if (today == file_date_) return;  // 双重检查

        file_.close();
        auto dst = dated_path(file_date_);
        std::error_code ec;
        std::filesystem::rename(path_, dst, ec);

        open_file();          // 打开新文件，更新 file_date_ 和 current_size_
        cleanup_old_files();  // 清理超出保留数的旧文件
    }

    /// 大小轮转：当前文件 → path_.YYYYMMDD.N.log，开新文件
    void rotate_size() {
        std::lock_guard lock(mutex_);
        if (current_size_ < max_file_size_) return;  // 双重检查

        file_.close();
        ++day_counter_;
        auto dst = dated_path(file_date_, day_counter_);
        std::error_code ec;
        std::filesystem::rename(path_, dst, ec);

        open_file();
        cleanup_old_files();
    }

    /// 日期文件名：path_.YYYYMMDD.log（无编号）或 path_.YYYYMMDD.N.log
    std::filesystem::path dated_path(const std::string& date, int index = 0) const {
        auto stem = path_.stem().string();
        auto ext = path_.extension().string();
        auto parent = path_.parent_path();
        if (index == 0) {
            return parent / (stem + "." + date + ext);
        }
        return parent / (stem + "." + date + "." + std::to_string(index) + ext);
    }

    /// 清理超出 max_rotated_files 的旧文件（按文件名排序，删最早）
    void cleanup_old_files() {
        if (max_rotated_files_ <= 0) return;

        auto dir = path_.parent_path();
        auto stem = path_.stem().string();
        auto ext = path_.extension().string();

        // 收集匹配模式 path_.YYYYMMDD*.log 的文件
        std::vector<std::filesystem::path> rotated;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            auto name = entry.path().filename().string();
            // 匹配 stem.YYYYMMDD[.N]ext 格式
            if (name.size() > stem.size() + 1 &&
                name.compare(0, stem.size(), stem) == 0 &&
                name[stem.size()] == '.') {
                auto rest = name.substr(stem.size() + 1);
                // 必须包含 ext 后缀
                if (rest.size() > ext.size() &&
                    rest.compare(rest.size() - ext.size(), ext.size(), ext) == 0) {
                    rotated.push_back(entry.path());
                }
            }
        }

        // 按路径名排序（自然顺序 = 时间顺序），删除超出数量的
        if (rotated.size() <= static_cast<size_t>(max_rotated_files_)) return;

        std::sort(rotated.begin(), rotated.end());
        auto to_delete = rotated.size() - static_cast<size_t>(max_rotated_files_);
        for (size_t i = 0; i < to_delete; ++i) {
            std::error_code ec2;
            std::filesystem::remove(rotated[i], ec2);
        }
    }

    // ---- 成员 ----

    std::filesystem::path path_;
    int flush_interval_ms_;
    int flush_batch_size_;
    size_t max_file_size_;
    int max_rotated_files_;

    std::string file_date_;          // 当前文件的日期 "YYYYMMDD"
    int day_counter_ = 0;            // 当天已轮转次数
    size_t current_size_ = 0;
    int unflushed_ = 0;
    std::chrono::steady_clock::time_point last_flush_;
    std::chrono::steady_clock::time_point last_day_check_;  // 按天检测节流
    std::string line_buf_;  // 行缓冲复用，避免每条日志分配

    std::ofstream file_;
    std::mutex mutex_;
};

using SinkList = std::vector<std::shared_ptr<Sink>>;

}  // namespace ben_gear::log
