#include "ben_gear/memory/episode.hpp"

#include <chrono>
#include <ctime>
#include <fstream>

namespace ben_gear::memory {

void EpisodeStore::append_today(const container::String& content) const {
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

container::String EpisodeStore::read_today() const {
    auto path = session_dir_ / "memory" / today_filename();
    return read_file(path);
}

container::Vector<container::String> EpisodeStore::read_range(
    const std::string& from_date,
    const std::string& to_date) const {
    container::Vector<container::String> results;

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

container::String EpisodeStore::read_file(const std::filesystem::path& path) {
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

}  // namespace ben_gear::memory
