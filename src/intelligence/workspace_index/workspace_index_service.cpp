#include "intelligence/workspace_index/workspace_index_service.hpp"

#include <filesystem>
#include <sstream>

namespace ben_gear::workspace_index {

namespace {

std::string to_std(const std::string& value) {
    return value;
}

} // namespace

std::string cache_key(const WorkspaceIndexOptions& options, const std::string& project_root, const std::string& signature) {
    std::ostringstream out;
    out << project_root << '|'
        << signature << '|'
        << options.max_files << '|'
        << options.max_symbols << '|'
        << options.max_dependencies << '|'
        << options.max_file_bytes << '|'
        << options.include_external << '|'
        << options.include_hidden;
    return out.str();
}

WorkspaceIndexService::WorkspaceIndexService(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

repo_map::RepoMapIndex WorkspaceIndexService::snapshot(const WorkspaceIndexOptions& options, BuildIndexFn build_index) {
    auto key = cache_key(options, project_root(), root_signature(options));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!options.refresh && cached_index_ && cached_key_ == key) {
            metrics_.cache_hit_count++;
            return *cached_index_;
        }
        if (options.refresh && cached_index_) metrics_.invalidated_count++;
        metrics_.cache_miss_count++;
    }

    auto built = build_index ? build_index() : repo_map::RepoMapIndex{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_key_ = std::move(key);
        cached_index_ = built;
        metrics_.index_build_count++;
    }
    return built;
}

void WorkspaceIndexService::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cached_index_) metrics_.invalidated_count++;
    cached_index_.reset();
    cached_key_.clear();
}

WorkspaceIndexMetrics WorkspaceIndexService::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

std::string WorkspaceIndexService::project_root() const {
    std::error_code ec;
    auto root = ws_ctx_.project_path.empty() ? std::filesystem::current_path(ec) : std::filesystem::path(to_std(ws_ctx_.project_path));
    auto canonical = std::filesystem::weakly_canonical(root, ec);
    if (!ec) return canonical.string();
    return root.string();
}

std::string WorkspaceIndexService::root_signature(const WorkspaceIndexOptions& options) const {
    auto root = std::filesystem::path(project_root());
    std::error_code ec;
    std::uintmax_t file_count = 0;
    std::uintmax_t size_sum = 0;
    std::uintmax_t mtime_sum = 0;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), root, ec);
        if (ec) continue;
        if (!options.include_hidden) {
            bool hidden = false;
            for (const auto& part : rel) {
                auto text = part.string();
                if (!text.empty() && text[0] == '.') {
                    hidden = true;
                    break;
                }
            }
            if (hidden) {
                if (entry.is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
        }
        if (!entry.is_regular_file(ec)) continue;
        file_count++;
        size_sum += entry.file_size(ec);
        if (ec) continue;
        auto time = entry.last_write_time(ec).time_since_epoch().count();
        if (!ec) mtime_sum += static_cast<std::uintmax_t>(time);
    }
    return std::to_string(file_count) + ":" + std::to_string(size_sum) + ":" + std::to_string(mtime_sum);
}

} // namespace ben_gear::workspace_index
