#pragma once

#include <string>
#include <vector>

namespace ben_gear::workspace { class HistoryDB; }

namespace ben_gear::memory {

/// 情景记忆存储（基于 HistoryDB，按 session_id + date 隔离）
class EpisodeStore {
public:
    EpisodeStore(workspace::HistoryDB& db, std::string session_id)
        : db_(db), session_id_(std::move(session_id)) {}

    /// 追加今日情景
    void append_today(const std::string& content) const;

    /// 读取今日情景
    std::string read_today() const;

    /// 读取日期范围内的情景（YYYYMMDD 格式）
    std::vector<std::string> read_range(
        const std::string& from_date,
        const std::string& to_date) const;

private:
    static std::string today_date();

    workspace::HistoryDB& db_;
    std::string session_id_;
};

}  // namespace ben_gear::memory
