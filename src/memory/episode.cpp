#include "memory/episode.hpp"
#include "workspace/history_db.hpp"

#include <chrono>
#include <ctime>
#include "platform/os.hpp"

namespace ben_gear::memory {

void EpisodeStore::append_today(const std::string& content) const {
    db_.append_episode(session_id_, today_date(), content);
}

std::string EpisodeStore::read_today() const {
    return db_.read_episode(session_id_, today_date());
}

std::vector<std::string> EpisodeStore::read_range(
    const std::string& from_date,
    const std::string& to_date) const {
    return db_.read_episodes_range(session_id_, from_date, to_date);
}

std::string EpisodeStore::today_date() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm_buf = ben_gear::base::platform::compat::safe_localtime(time_t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm_buf);
    return buf;
}

}  // namespace ben_gear::memory
