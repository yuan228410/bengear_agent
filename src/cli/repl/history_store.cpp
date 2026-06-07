#include "ben_gear/cli/repl/history_store.hpp"

#include <fstream>
#include <sstream>

namespace ben_gear::cli {

void HistoryStore::load(const std::filesystem::path& path) {
    entries_.clear();
    nav_pos_ = 0;

    if (!std::filesystem::exists(path)) return;

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        entries_.emplace_back(line.c_str());
    }
    // 去重：连续相同只保留一个
    if (entries_.size() > 1) {
        size_t write = 0;
        for (size_t read = 0; read < entries_.size(); ++read) {
            if (write == 0 || entries_[read] != entries_[write - 1]) {
                if (write != read) entries_[write] = std::move(entries_[read]);
                ++write;
            }
        }
        entries_.resize(write);
    }
}

void HistoryStore::save(const std::filesystem::path& path) const {
    // 确保目录存在
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;

    for (const auto& entry : entries_) {
        file << std::string(entry.data(), entry.size()) << '\n';
    }
}

std::filesystem::path HistoryStore::default_path() {
    // ~/.bengear/history
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = getenv("APPDATA");
#endif
    if (!home) home = "/tmp";
    return std::filesystem::path(home) / ".bengear" / "history";
}

void HistoryStore::add(std::string_view line) {
    if (line.empty()) return;
    // 与最后一条相同则跳过
    if (!entries_.empty()) {
        auto& last = entries_.back();
        if (line.size() == last.size() &&
            std::memcmp(line.data(), last.data(), line.size()) == 0) {
            return;
        }
    }
    entries_.emplace_back(container::String(line.data(), line.size()));
    // 限制条数
    if (entries_.size() > kMaxEntries) {
        entries_.erase(entries_.begin());
    }
    nav_pos_ = 0;
}

std::string_view HistoryStore::up() {
    if (entries_.empty()) return {};
    if (nav_pos_ < entries_.size()) ++nav_pos_;
    return current();
}

std::string_view HistoryStore::down() {
    if (nav_pos_ > 0) --nav_pos_;
    return current();
}

void HistoryStore::reset_nav() {
    nav_pos_ = 0;
}

std::string_view HistoryStore::current() const {
    if (nav_pos_ == 0 || nav_pos_ > entries_.size()) return {};
    // nav_pos_ = 1 → entries_[size-1]（最新），nav_pos_ = size → entries_[0]（最老）
    return {entries_[entries_.size() - nav_pos_].data(),
            entries_[entries_.size() - nav_pos_].size()};
}

}  // namespace ben_gear::cli
