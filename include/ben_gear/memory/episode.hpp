#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 情景记忆存储（按日期存储到独立文件）
class EpisodeStore {
public:
    explicit EpisodeStore(const std::filesystem::path& session_dir)
        : session_dir_(session_dir) {
        std::filesystem::create_directories(session_dir_);
    }

    /// 追加今日情景
    void append_today(const container::String& content) const;

    /// 读取今日情景
    container::String read_today() const;

    /// 读取日期范围内的情景（YYYYMMDD 格式）
    container::Vector<container::String> read_range(
        const std::string& from_date,
        const std::string& to_date) const;

    /// 获取 session 目录
    const std::filesystem::path& session_dir() const { return session_dir_; }

private:
    static std::string today_filename();
    static container::String read_file(const std::filesystem::path& path);

    std::filesystem::path session_dir_;
};

}  // namespace ben_gear::memory
