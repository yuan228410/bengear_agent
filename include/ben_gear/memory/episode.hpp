#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/file_lock.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 每日情景记忆存储（实例化风格，与 MemoryStore 统一）
/// 每个会话有自己的 episode 目录，每天一个 YYYYMMDD.md 文件
/// 跨进程安全：通过 FileLock 实现文件级互斥
class EpisodeStore {
public:
    /// 构造时绑定 session 目录
    explicit EpisodeStore(const std::filesystem::path& session_dir)
        : session_dir_(session_dir) {}

    /// 追加内容到今日情景文件
    void append_today(const container::String& content) const {
        auto dir = session_dir_ / "memory_data";
        std::filesystem::create_directories(dir);

        auto path = dir / today_filename();

        // 获取排他文件锁（跨进程安全，RAII 自动释放）
        auto lock = base::platform::FileLock::exclusive(path);
        if (!lock) {
            log::error_fmt("episode append: failed to acquire lock: {}", path.string());
            return;
        }

        // 追加写入：先 seek 到文件末尾
        lock->seek(0, SEEK_END);

        std::string data(content.data(), content.size());
        data += '\n';
        auto written = lock->write(data.data(), data.size());
        if (written < 0 || static_cast<size_t>(written) != data.size()) {
            log::error_fmt("episode write failed: path={}", path.string());
            return;
        }

        log::debug_fmt("episode appended: size={}", content.size());
    }

    /// 读取今日情景
    container::String read_today() const {
        auto path = session_dir_ / "memory_data" / today_filename();
        return read_file(path);
    }

    /// 读取指定日期范围的情景
    container::Vector<container::String> read_range(
        const std::string& from_date,   // YYYY-MM-DD
        const std::string& to_date) const {  // YYYY-MM-DD
        container::Vector<container::String> results;
        auto dir = session_dir_ / "memory_data";
        if (!std::filesystem::exists(dir)) return results;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().string();
            // 文件名格式：YYYYMMDD.md（12 字符）
            if (name.size() != 12 || name.substr(8) != ".md") continue;
            auto date = name.substr(0, 8);
            // 转为 YYYY-MM-DD 格式比较
            std::string formatted = date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2);
            if (formatted >= from_date && formatted <= to_date) {
                results.push_back(read_file(entry.path()));
            }
        }
        return results;
    }

    /// 获取 session 目录
    const std::filesystem::path& session_dir() const { return session_dir_; }

private:
    /// 生成今日文件名 YYYYMMDD.md
    static std::string today_filename() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y%m%d.md", &tm_buf);
        return buf;
    }

    static container::String read_file(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return container::String();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return container::String();
        auto size = file.tellg();
        if (size <= 0) return container::String();
        file.seekg(0, std::ios::beg);
        std::vector<char> buf(static_cast<size_t>(size));
        file.read(buf.data(), static_cast<std::streamsize>(size));
        if (!file) return container::String();
        return container::String(buf.data(), static_cast<size_t>(size));
    }

    std::filesystem::path session_dir_;
};

}  // namespace ben_gear::memory
