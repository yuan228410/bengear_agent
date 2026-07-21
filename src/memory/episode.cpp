#include "memory/episode.hpp"
#include <filesystem>

#include <chrono>
#include <ctime>
#include <fstream>
#include "log/logger.hpp"
#include "platform/os.hpp"

namespace ben_gear::memory {

void EpisodeStore::append_today(const std::string& content) const {
    auto dir = session_dir_ / "memory";
    std::filesystem::create_directories(dir);
    auto filename = today_filename();
    auto path = dir / filename;

    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) {
        log::error_fmt("episode write failed: {}", path.string());
        return;
    }
    file << content.data() << "\n\n";
    log::info_fmt("episode written: file={}, size={}", filename,
                  content.size());
}

std::string EpisodeStore::read_today() const {
    auto path = session_dir_ / "memory" / today_filename();
    return read_file(path);
}

std::vector<std::string> EpisodeStore::read_range(
    const std::string& from_date,
    const std::string& to_date) const {
    std::vector<std::string> results;

    auto dir = session_dir_ / "memory";
    if (!std::filesystem::exists(dir)) return results;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (name.size() < 10 || name.substr(8) != ".md") continue;

        std::string formatted = name.substr(0, 8);
        if (formatted >= from_date && formatted <= to_date) {
            results.push_back(read_file(entry.path()));
        }
    }
    return results;
}

std::string EpisodeStore::today_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm_buf = ben_gear::base::platform::compat::safe_localtime(time_t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d.md", &tm_buf);
    return buf;
}

std::string EpisodeStore::read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::string();
    auto size = file.tellg();
    if (size <= 0) return std::string();
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), static_cast<std::streamsize>(size));
    if (!file) return std::string();
    return std::string(buf.data(), static_cast<size_t>(size));
}

}  // namespace ben_gear::memory
