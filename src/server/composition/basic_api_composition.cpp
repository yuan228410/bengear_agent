#include "server/composition/basic_api_composition.hpp"

#include "capabilities/audit/audit_store.hpp"
#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include "workspace/history_exporter.hpp"
#include "workspace/manager.hpp"
#include "workspace/uuid.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ben_gear::server::composition {

namespace {

namespace container = base::container;

container::String workspace_or_default(BasicApiCompositionContext context, const container::String& workspace) {
    return context.workspace_resolver.workspace_or_default(workspace);
}

workspace::WorkspaceContext workspace_context(BasicApiCompositionContext context,
                                               const container::String& workspace,
                                               const container::String& session_id,
                                               const container::String& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    request.session_id = session_id;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context() : workspace::WorkspaceContext{};
}

container::String formatted_modified_time(const std::filesystem::directory_entry& entry) {
    auto ft = entry.last_write_time();
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    auto tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return container::String(oss.str().c_str());
}

} // namespace

SessionService make_session_api_service(BasicApiCompositionContext context) {
    SessionService svc;
    svc.get_user_dir = [context](const container::String& username) {
        return context.workspace_resolver.user_dir_for(username);
    };
    svc.list_sessions = [context](const container::String& workspace, const container::String& username) {
        auto ws = workspace_or_default(context, workspace);
        workspace::HistoryDB db(context.workspace_resolver.user_dir_for(username) / "history.db");
        return db.list_sessions(ws);
    };
    svc.list_sessions_by_workspace = [context](const container::String& ws_name, const container::String& username) {
        workspace::HistoryDB db(context.workspace_resolver.user_dir_for(username) / "history.db");
        return db.list_sessions(ws_name);
    };
    svc.create_session = [context](const container::String& name, const container::String& ws_name, const container::String& username) {
        auto sid = std::string(workspace::generate_uuid().c_str());
        auto ws = workspace_or_default(context, ws_name);
        auto ws_ctx = workspace_context(context, ws, container::String(sid.c_str()), username);
        log::info_fmt("Server: create_session user={} workspace={} session={} project_path={}",
                      username.c_str(), ws.c_str(), sid.c_str(), ws_ctx.project_path.c_str());
        auto entry = context.session_pool.get_or_create(container::String(sid.c_str()), username, ws, context.settings, ws_ctx);
        entry->agent->history_db().create_session(ws, container::String(sid.c_str()), name);
        return container::String(sid.c_str());
    };
    svc.delete_session = [context](const container::String& sid, const container::String& workspace, const container::String& username) {
        auto ws = workspace_or_default(context, workspace);
        auto db_path = context.workspace_resolver.user_dir_for(username) / "history.db";
        log::info_fmt("API delete_session: sid={} ws={} user={} db_path={}",
                      sid.c_str(), ws.c_str(), username.c_str(), db_path.string().c_str());
        workspace::HistoryDB db(db_path);
        auto ok = db.delete_session(ws, sid);
        if (ok) log::info_fmt("API delete_session: DB delete OK");
        else log::error_fmt("API delete_session: DB delete FAILED");
        context.session_pool.remove(sid, username, ws);
        return ok;
    };
    svc.rename_session = [context](const container::String& sid, const container::String& name, const container::String& workspace, const container::String& username) {
        auto ws = workspace_or_default(context, workspace);
        if (auto entry = context.session_pool.get(sid, username, ws)) return entry->agent->history_db().rename_session(ws, sid, name);
        workspace::HistoryDB db(context.workspace_resolver.user_dir_for(username) / "history.db");
        return db.rename_session(ws, sid, name);
    };
    svc.load_history = [context](const container::String& sid, const container::String& ws_name, int limit, const container::String& username) {
        auto ws = workspace_or_default(context, ws_name);
        workspace::HistoryDB db(context.workspace_resolver.user_dir_for(username) / "history.db");
        return db.load_session_chat_messages(ws, sid, limit);
    };
    svc.export_history = [context](const container::String& sid,
                                   const container::String& ws_name,
                                   bool include_tool_calls,
                                   bool include_thinking,
                                   bool include_tool_results,
                                   int limit,
                                   const container::String& username) {
        auto ws = workspace_or_default(context, ws_name);
        workspace::HistoryDB db(context.workspace_resolver.user_dir_for(username) / "history.db");
        workspace::ExportOptions opts;
        opts.include_tool_calls = include_tool_calls;
        opts.include_thinking = include_thinking;
        opts.include_tool_results = include_tool_results;
        opts.limit = limit;
        return workspace::HistoryExporter::export_session_md(db, ws, sid, opts);
    };
    return svc;
}

ConfigService make_config_api_service(BasicApiCompositionContext context) {
    ConfigService svc;
    svc.get_config = [context]() {
        ConfigInfo info;
        info.model = context.settings.model;
        info.provider = provider_name(context.settings.provider);
        info.workspace = context.settings.workspace_name.empty() ? container::String("default") : context.settings.workspace_name;
        info.display_name = context.settings.display_name;
        info.version = container::String(BEN_GEAR_VERSION);
        return info;
    };
    svc.set_model = [context](const container::String& model) {
        context.settings.model = model;
    };
    return svc;
}

WorkspaceService make_workspace_api_service(BasicApiCompositionContext context) {
    WorkspaceService svc;
    svc.list_workspaces = [context](const container::String& username) {
        workspace::WorkspaceManager mgr(context.workspace_resolver.user_dir_for(username));
        container::Vector<WorkspaceInfo> result;
        auto metas = mgr.list_all();
        for (const auto& meta : metas) {
            WorkspaceInfo info;
            info.name = meta.name;
            info.path = std::string(meta.project_path.data(), meta.project_path.size());
            result.push_back(std::move(info));
        }
        return result;
    };
    svc.create_workspace = [context](const container::String& name, const container::String& project_path, const container::String& username) {
        workspace::WorkspaceManager mgr(context.workspace_resolver.user_dir_for(username));
        auto meta = mgr.create(name, project_path);
        if (!meta) return std::optional<WorkspaceInfo>();
        WorkspaceInfo info;
        info.name = meta->name;
        info.path = std::string(meta->project_path.data(), meta->project_path.size());
        return std::optional<WorkspaceInfo>(std::move(info));
    };
    svc.delete_workspace = [context](const container::String& name, const container::String& username) {
        workspace::WorkspaceManager mgr(context.workspace_resolver.user_dir_for(username));
        return mgr.remove(name);
    };
    return svc;
}

McpService make_mcp_api_service() {
    McpService svc;
    svc.get_status = []() { return std::string(R"({"servers":[]})"); };
    return svc;
}

FileService make_file_api_service() {
    FileService svc;
    svc.home_directory = [](const container::String&) {
        return container::String(ben_gear::support::home_directory().string().c_str());
    };
    svc.list_files = [](const container::String& path, const container::String&) {
        container::Vector<FileEntry> entries;
        auto dir_path = std::string(path.empty() ? "/" : path.c_str());
        try {
            if (!std::filesystem::exists(dir_path)) return entries;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                FileEntry file;
                file.name = container::String(entry.path().filename().string().c_str());
                file.type = container::String(entry.is_directory() ? "dir" : "file");
                if (entry.is_regular_file()) file.size = entry.file_size();
                file.modified = formatted_modified_time(entry);
                entries.push_back(std::move(file));
            }
        } catch (const std::exception& e) {
            log::error_fmt("FileService: list_files error for {}: {}", dir_path, e.what());
        }
        return entries;
    };
    return svc;
}

AuditApiService make_audit_api_service(BasicApiCompositionContext context) {
    AuditApiService svc;
    svc.list_events = [context](const container::String& workspace,
                                const container::String& session_id,
                                const container::String& username,
                                const container::String& category,
                                const container::String& action,
                                int limit) {
        audit::AuditQuery query;
        query.workspace = workspace_or_default(context, workspace);
        query.session_id = session_id;
        query.category = category;
        query.action = action;
        query.limit = limit;
        audit::AuditStore store(context.workspace_resolver.user_dir_for(username) / "audit" / "events.jsonl");
        return store.list(query);
    };
    return svc;
}

} // namespace ben_gear::server::composition
