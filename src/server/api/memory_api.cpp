#include "server/api/memory_api.hpp"
#include "log/logger.hpp"
#include "platform/file_lock.hpp"

#include <fstream>
#include <filesystem>
#include <string>
#include <regex>

namespace ben_gear::server {

namespace {

/// kind → 文件名映射
const char* kind_to_filename(const std::string& kind) {
    if (kind == "soul")   return "SOUL.md";
    if (kind == "memory") return "MEMORY.md";
    if (kind == "rules")  return "RULES.md";
    if (kind == "user")   return "USER.md";
    return nullptr;
}

/// tier 白名单校验
bool is_valid_tier(const std::string& tier) {
    return tier == "global" || tier == "user" || tier == "workspace";
}

/// kind 白名单校验
bool is_valid_kind(const std::string& kind) {
    return kind == "soul" || kind == "memory" || kind == "rules" || kind == "user";
}

/// 日期格式校验（YYYYMMDD）
bool is_valid_date(const std::string& date) {
    static const std::regex pattern(R"(^\d{8}$)");
    return std::regex_match(date, pattern);
}

/// 读取文件内容
std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), static_cast<std::streamsize>(size));
    if (!file) return {};
    return std::string(buf.data(), static_cast<size_t>(size));
}

/// 获取文件大小
int64_t get_file_size(const std::filesystem::path& path) {
    std::error_code ec;
    auto s = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<int64_t>(s);
}

/// 原子写入文件（文件锁 + truncate + fsync + chmod 600）
bool write_file_atomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    auto lock = base::platform::FileLock::exclusive(path);
    if (!lock) {
        log::error_fmt("memory_api: failed to acquire lock: {}", path.string());
        return false;
    }
    if (!lock->truncate(0)) {
        log::error_fmt("memory_api: failed to truncate: {}", path.string());
        return false;
    }
    auto written = lock->write(content.data(), content.size());
    if (written != static_cast<ssize_t>(content.size())) {
        log::error_fmt("memory_api: partial write ({}/{}) to {}", written, content.size(), path.string());
        return false;
    }
    if (!lock->sync()) {
        log::warn_fmt("memory_api: fsync failed: {}", path.string());
    }
    try {
        std::filesystem::permissions(path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
    } catch (...) {}
    return true;
}

/// 删除文件
bool delete_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

/// 解析 JSON body
base::json::Json parse_body(const std::string& body) {
    return base::json::Json::parse(body);
}

} // namespace

void register_memory_routes(Router& router, const workspace::WorkspaceResolver& resolver) {

    // ── 记忆文件列表 ──────────────────────────────────────
    router.add_route("GET", "/api/memory/list",
        [&resolver](const HttpRequest& req) {
            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);

            base::json::Json result = base::json::Json::array();
            const char* kinds[] = {"soul", "memory", "rules", "user"};
            const char* tiers[] = {"global", "user", "workspace"};

            for (const char* tier_name : tiers) {
                auto tier = base::TierPaths::tier_from_name(tier_name);
                auto dir = tier_paths.dir(tier) / "memory";
                for (const char* kind : kinds) {
                    auto filename = kind_to_filename(kind);
                    auto path = dir / filename;
                    bool exists = std::filesystem::exists(path);
                    base::json::Json item;
                    item["tier"] = tier_name;
                    item["kind"] = kind;
                    item["exists"] = exists;
                    item["size"] = exists ? get_file_size(path) : 0;
                    result.push_back(std::move(item));
                }
            }
            return HttpResponse::ok(result.dump());
        });

    // ── 读取记忆文件 ──────────────────────────────────────
    router.add_route("GET", "/api/memory/read",
        [&resolver](const HttpRequest& req) {
            auto tier_it = req.query.find("tier");
            auto kind_it = req.query.find("kind");
            if (tier_it == req.query.end() || kind_it == req.query.end())
                return HttpResponse::error(400, "missing tier or kind");
            if (!is_valid_tier(tier_it->second) || !is_valid_kind(kind_it->second))
                return HttpResponse::error(400, "invalid tier or kind");

            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto tier = base::TierPaths::tier_from_name(tier_it->second);
            auto path = tier_paths.dir(tier) / "memory" / kind_to_filename(kind_it->second);

            if (!std::filesystem::exists(path))
                return HttpResponse::ok("{\"content\":\"\"}");

            auto content = read_file_content(path);
            base::json::Json result;
            result["content"] = content;
            return HttpResponse::ok(result.dump());
        });

    // ── 写入记忆文件 ──────────────────────────────────────
    router.add_route("POST", "/api/memory/write",
        [&resolver](const HttpRequest& req) {
            auto body = parse_body(req.body);
            auto tier = body.value("tier", "");
            auto kind = body.value("kind", "");
            auto content = body.value("content", "");

            if (!is_valid_tier(tier) || !is_valid_kind(kind))
                return HttpResponse::error(400, "invalid tier or kind");

            auto ws = body.value("workspace", "default");
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto t = base::TierPaths::tier_from_name(tier);
            auto path = tier_paths.dir(t) / "memory" / kind_to_filename(kind);

            if (!write_file_atomic(path, content))
                return HttpResponse::error(500, "write failed");

            log::info_fmt("memory_api: write tier={} kind={} size={}", tier, kind, content.size());
            return HttpResponse::ok();
        });

    // ── 删除记忆文件 ──────────────────────────────────────
    router.add_route("DELETE", "/api/memory/delete",
        [&resolver](const HttpRequest& req) {
            auto tier_it = req.query.find("tier");
            auto kind_it = req.query.find("kind");
            if (tier_it == req.query.end() || kind_it == req.query.end())
                return HttpResponse::error(400, "missing tier or kind");
            if (!is_valid_tier(tier_it->second) || !is_valid_kind(kind_it->second))
                return HttpResponse::error(400, "invalid tier or kind");

            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto tier = base::TierPaths::tier_from_name(tier_it->second);
            auto path = tier_paths.dir(tier) / "memory" / kind_to_filename(kind_it->second);

            if (!std::filesystem::exists(path))
                return HttpResponse::error(404, "file not found");

            if (!delete_file(path))
                return HttpResponse::error(500, "delete failed");

            log::info_fmt("memory_api: delete tier={} kind={}", tier_it->second, kind_it->second);
            return HttpResponse::ok();
        });

    // ── 情景记忆列表 ──────────────────────────────────────
    router.add_route("GET", "/api/memory/episodes",
        [&resolver](const HttpRequest& req) {
            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            // 情景记忆在 workspace 层 sessions 目录下，按 session 分子目录
            // 但 EpisodeStore 按 session_dir/memory/ 存储
            // 这里列出 workspace 下所有 session 的情景记忆
            auto ws_dir = tier_paths.workspace_dir;
            auto sessions_dir = ws_dir / "sessions";

            base::json::Json result = base::json::Json::array();
            if (!std::filesystem::exists(sessions_dir))
                return HttpResponse::ok(result.dump());

            // 遍历所有 session 目录，收集 memory/*.md 文件
            for (const auto& session_entry : std::filesystem::directory_iterator(sessions_dir)) {
                if (!session_entry.is_directory()) continue;
                auto session_id = session_entry.path().filename().string();
                auto mem_dir = session_entry.path() / "memory";
                if (!std::filesystem::exists(mem_dir)) continue;

                for (const auto& file_entry : std::filesystem::directory_iterator(mem_dir)) {
                    if (!file_entry.is_regular_file()) continue;
                    auto filename = file_entry.path().filename().string();
                    // 只关心 YYYYMMDD.md 格式
                    if (filename.size() != 12 || !filename.ends_with(".md")) continue;
                    auto date = filename.substr(0, 8);
                    if (!is_valid_date(date)) continue;

                    base::json::Json item;
                    item["session_id"] = session_id;
                    item["date"] = date;
                    item["size"] = get_file_size(file_entry.path());
                    result.push_back(std::move(item));
                }
            }
            return HttpResponse::ok(result.dump());
        });

    // ── 读取情景记忆 ──────────────────────────────────────
    router.add_route("GET", "/api/memory/episode/read",
        [&resolver](const HttpRequest& req) {
            auto ws_it = req.query.find("workspace");
            auto date_it = req.query.find("date");
            auto sid_it = req.query.find("session_id");
            if (date_it == req.query.end() || sid_it == req.query.end())
                return HttpResponse::error(400, "missing date or session_id");
            if (!is_valid_date(date_it->second))
                return HttpResponse::error(400, "invalid date");

            auto ws = (ws_it != req.query.end()) ? ws_it->second : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto path = tier_paths.workspace_dir / "sessions" / sid_it->second / "memory" / (date_it->second + ".md");

            if (!std::filesystem::exists(path))
                return HttpResponse::ok("{\"content\":\"\"}");

            auto content = read_file_content(path);
            base::json::Json result;
            result["content"] = content;
            return HttpResponse::ok(result.dump());
        });

    // ── 写入情景记忆 ──────────────────────────────────────
    router.add_route("POST", "/api/memory/episode/write",
        [&resolver](const HttpRequest& req) {
            auto body = parse_body(req.body);
            auto ws = body.value("workspace", "default");
            auto session_id = body.value("session_id", "");
            auto date = body.value("date", "");
            auto content = body.value("content", "");

            if (session_id.empty() || !is_valid_date(date))
                return HttpResponse::error(400, "invalid session_id or date");

            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto path = tier_paths.workspace_dir / "sessions" / session_id / "memory" / (date + ".md");

            if (!write_file_atomic(path, content))
                return HttpResponse::error(500, "write failed");

            log::info_fmt("memory_api: episode write session={} date={} size={}", session_id, date, content.size());
            return HttpResponse::ok();
        });

    // ── 删除情景记忆 ──────────────────────────────────────
    router.add_route("DELETE", "/api/memory/episode/delete",
        [&resolver](const HttpRequest& req) {
            auto ws_it = req.query.find("workspace");
            auto date_it = req.query.find("date");
            auto sid_it = req.query.find("session_id");
            if (date_it == req.query.end() || sid_it == req.query.end())
                return HttpResponse::error(400, "missing date or session_id");
            if (!is_valid_date(date_it->second))
                return HttpResponse::error(400, "invalid date");

            auto ws = (ws_it != req.query.end()) ? ws_it->second : "default";
            auto tier_paths = resolver.tier_paths_for(req.username, ws);
            auto path = tier_paths.workspace_dir / "sessions" / sid_it->second / "memory" / (date_it->second + ".md");

            if (!std::filesystem::exists(path))
                return HttpResponse::error(404, "file not found");

            if (!delete_file(path))
                return HttpResponse::error(500, "delete failed");

            log::info_fmt("memory_api: episode delete session={} date={}", sid_it->second, date_it->second);
            return HttpResponse::ok();
        });

    log::info_fmt("API: memory routes registered (8)");
}

} // namespace ben_gear::server
