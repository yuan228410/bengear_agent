#include "server/composition/basic_api_composition.hpp"

#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include "workspace/history_exporter.hpp"
#include "workspace/manager.hpp"
#include "base/utils/uuid.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <memory>
#include "workspace/history_db.hpp"

namespace ben_gear::server::composition {

namespace {

std::string workspace_or_default(BasicApiCompositionContext context, const std::string& workspace) {
    return context.workspace_resolver.workspace_or_default(workspace);
}

workspace::WorkspaceContext workspace_context(BasicApiCompositionContext context,
                                               const std::string& workspace,
                                               const std::string& session_id,
                                               const std::string& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    request.session_id = session_id;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context() : workspace::WorkspaceContext{};
}

std::string formatted_modified_time(const std::filesystem::directory_entry& entry) {
    auto ft = entry.last_write_time();
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    auto tt = std::chrono::system_clock::to_time_t(sctp);
    auto tm = ben_gear::base::platform::compat::safe_localtime(tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

} // namespace

// --- Concrete service implementations ---

class BasicSessionService : public SessionService {
public:
    explicit BasicSessionService(BasicApiCompositionContext context) : ctx_(context) {}
    workspace::HistoryDB& db() { return *ctx_.history_db; }

    std::filesystem::path get_user_dir(const std::string& username) override {
        return ctx_.workspace_resolver.user_dir_for(username);
    }

    std::vector<Json> list_sessions(const std::string& workspace, const std::string& username) override {
        auto ws = workspace_or_default(ctx_, workspace);
        return db().list_sessions(username, ws);
    }

    std::vector<Json> list_sessions_by_workspace(const std::string& ws_name, const std::string& username) override {
        auto ws = workspace_or_default(ctx_, ws_name);
        return db().list_sessions(username, ws);
    }

    std::string create_session(const std::string& name, const std::string& ws_name, const std::string& username) override {
        auto sid = base::utils::generate_uuid();
        auto ws = workspace_or_default(ctx_, ws_name);
        auto ws_ctx = workspace_context(ctx_, ws, sid, username);
        log::info_fmt("Server: create_session user={} workspace={} session={} project_path={}",
                      username.c_str(), ws.c_str(), sid.c_str(), ws_ctx.project_path.c_str());
        auto entry = ctx_.session_pool.get_or_create(sid, username, ws, ctx_.settings, ws_ctx);
        entry->runtime->history_db().create_session(username, ws, sid, name);
        return sid;
    }
    bool delete_session(const std::string& sid, const std::string& workspace, const std::string& username) override {
        auto ws = workspace_or_default(ctx_, workspace);
        auto ok = db().delete_session(sid);
        if (ok) log::info_fmt("API delete_session: DB delete OK");
        else log::error_fmt("API delete_session: DB delete FAILED");
        ctx_.session_pool.remove(sid, username, ws);
        return ok;
    }
    bool rename_session(const std::string& sid, const std::string& name, const std::string& workspace, const std::string& username) override {
        auto ws = workspace_or_default(ctx_, workspace);
        if (auto entry = ctx_.session_pool.get(sid, username, ws))
            return entry->runtime->history_db().rename_session(sid, name);
        return db().rename_session(sid, name);
    }

    std::vector<Json> load_history(const std::string& sid, const std::string&, int limit, const std::string&) override {
        return db().load_session_chat_messages(sid, limit);
    }

    std::string export_history(const std::string& sid,
                               const std::string&,
                               bool include_tool_calls,
                               bool include_thinking,
                               bool include_tool_results,
                               int limit,
                               const std::string&) override {
        workspace::ExportOptions opts;
        opts.include_tool_calls = include_tool_calls;
        opts.include_thinking = include_thinking;
        opts.include_tool_results = include_tool_results;
        opts.limit = limit;
        return workspace::HistoryExporter::export_session_md(db(), sid, opts);
    }

private:
    BasicApiCompositionContext ctx_;
};


class BasicConfigService : public ConfigService {
public:
    explicit BasicConfigService(BasicApiCompositionContext context) : ctx_(context) {}

    ConfigInfo get_config() override {
        ConfigInfo info;
        info.model = ctx_.settings.llm.model;
        info.provider = provider_name(ctx_.settings.llm.provider);
        info.workspace = ctx_.settings.workspace_name.empty() ? std::string("default") : ctx_.settings.workspace_name;
        info.display_name = ctx_.settings.llm.display_name;
        info.version = std::string(BEN_GEAR_VERSION);
        return info;
    }

    void set_model(const std::string& model) override {
        ctx_.settings.llm.model = model;
    }

private:
    BasicApiCompositionContext ctx_;
};

class BasicWorkspaceService : public WorkspaceService {
public:
    explicit BasicWorkspaceService(BasicApiCompositionContext context) : ctx_(context) {}

    std::vector<WorkspaceInfo> list_workspaces(const std::string& username) override {
        workspace::WorkspaceManager mgr(ctx_.workspace_resolver.user_dir_for(username));
        std::vector<WorkspaceInfo> result;
        auto metas = mgr.list_all();
        for (const auto& meta : metas) {
            WorkspaceInfo info;
            info.name = meta.name;
            info.path = std::string(meta.project_path.data(), meta.project_path.size());
            result.push_back(std::move(info));
        }
        return result;
    }

    std::optional<WorkspaceInfo> create_workspace(const std::string& name, const std::string& project_path, const std::string& username) override {
        workspace::WorkspaceManager mgr(ctx_.workspace_resolver.user_dir_for(username));
        auto meta = mgr.create(name, project_path);
        if (!meta) return std::optional<WorkspaceInfo>();
        WorkspaceInfo info;
        info.name = meta->name;
        info.path = std::string(meta->project_path.data(), meta->project_path.size());
        return std::optional<WorkspaceInfo>(std::move(info));
    }

    bool delete_workspace(const std::string& name, const std::string& username) override {
        workspace::WorkspaceManager mgr(ctx_.workspace_resolver.user_dir_for(username));
        return mgr.remove(name);
    }

private:
    BasicApiCompositionContext ctx_;
};

class BasicMcpService : public McpService {
public:
    std::string get_status() override { return std::string(R"({"servers":[]})"); }
};

class BasicFileService : public FileService {
public:
    std::vector<FileEntry> list_files(const std::string& path, const std::string& /*username*/) override {
        std::vector<FileEntry> entries;
        auto dir_path = path.empty() ? "/" : path;
        try {
            if (!std::filesystem::exists(dir_path)) return entries;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                FileEntry file;
                file.name = entry.path().filename().string();
                file.type = std::string(entry.is_directory() ? "dir" : "file");
                if (entry.is_regular_file()) file.size = entry.file_size();
                file.modified = formatted_modified_time(entry);
                entries.push_back(std::move(file));
            }
        } catch (const std::exception& e) {
            log::error_fmt("FileService: list_files error for {}: {}", dir_path, e.what());
        }
        return entries;
    }

    std::string home_directory(const std::string& /*username*/) override {
        return ben_gear::support::home_directory().string();
    }
};

// --- Factory functions ---

std::shared_ptr<SessionService> make_session_api_service(BasicApiCompositionContext context) {
    return std::make_shared<BasicSessionService>(context);
}

std::shared_ptr<ConfigService> make_config_api_service(BasicApiCompositionContext context) {
    return std::make_shared<BasicConfigService>(context);
}

std::shared_ptr<WorkspaceService> make_workspace_api_service(BasicApiCompositionContext context) {
    return std::make_shared<BasicWorkspaceService>(context);
}

std::shared_ptr<McpService> make_mcp_api_service() {
    return std::make_shared<BasicMcpService>();
}

std::shared_ptr<FileService> make_file_api_service() {
    return std::make_shared<BasicFileService>();
}
} // namespace ben_gear::server::composition
