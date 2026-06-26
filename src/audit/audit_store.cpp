#include "ben_gear/audit/audit_store.hpp"

#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/workspace/uuid.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace ben_gear::audit {

namespace {

std::mutex& audit_file_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& runtime_execution_file_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& runtime_execution_link_file_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& runtime_workflow_file_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string as_string(const container::String& value) {
    return std::string(value.data(), value.size());
}

bool matches_field(const Json& event, std::string_view key, const container::String& expected) {
    if (expected.empty()) return true;
    return event.value(std::string(key), "") == as_string(expected);
}

bool matches_operation_capability(const Json& event, const container::String& expected) {
    if (expected.empty()) return true;
    if (!event.contains("operation") || !event["operation"].is_object()) return false;
    return event["operation"].value("capability", "") == as_string(expected);
}

bool matches_link_execution(const Json& link, const container::String& execution_id) {
    if (execution_id.empty()) return true;
    auto value = as_string(execution_id);
    return link.value("source_execution_id", "") == value || link.value("target_execution_id", "") == value;
}

bool matches_workflow_source(const Json& workflow, const container::String& source_execution_id) {
    if (source_execution_id.empty()) return true;
    return workflow.value("source_execution_id", "") == as_string(source_execution_id);
}

} // namespace

AuditStore::AuditStore(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

Json AuditStore::append(Json event) const {
    if (!event.is_object()) event = Json::object();
    if (!event.contains("event_id") || event.value("event_id", "").empty()) {
        event["event_id"] = std::string(workspace::generate_uuid().c_str());
    }
    if (!event.contains("ts") || event.value("ts", "").empty()) event["ts"] = now_iso();

    try {
        std::filesystem::create_directories(file_path_.parent_path());
        std::lock_guard<std::mutex> lock(audit_file_mutex());
        std::ofstream out(file_path_, std::ios::app | std::ios::binary);
        if (!out) return Json{{"success", false}, {"error_type", "audit_open_failed"}, {"message", "failed to open audit log"}};
        out << event.dump().to_std_string() << '\n';
        return Json{{"success", true}, {"event", event}};
    } catch (const std::exception& e) {
        log::error_fmt("AuditStore append failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "audit_append_failed"}, {"message", e.what()}};
    }
}

Json AuditStore::list(const AuditQuery& query) const {
    std::vector<Json> matched;
    try {
        std::lock_guard<std::mutex> lock(audit_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", true}, {"events", Json::array()}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto event = Json::parse(line);
                if (!event.is_object()) continue;
                if (!matches_field(event, "workspace", query.workspace)) continue;
                if (!matches_field(event, "session_id", query.session_id)) continue;
                if (!matches_field(event, "category", query.category)) continue;
                if (!matches_field(event, "action", query.action)) continue;
                matched.push_back(std::move(event));
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        log::error_fmt("AuditStore list failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "audit_read_failed"}, {"message", e.what()}, {"events", Json::array()}};
    }

    Json events = Json::array();
    auto limit = query.limit > 0 ? query.limit : 100;
    for (auto it = matched.rbegin(); it != matched.rend() && events.size() < static_cast<size_t>(limit); ++it) {
        events.push_back(*it);
    }
    return Json{{"success", true}, {"events", events}};
}




RuntimeWorkflowStore::RuntimeWorkflowStore(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

Json RuntimeWorkflowStore::append(Json workflow) const {
    if (!workflow.is_object()) workflow = Json::object();
    if (!workflow.contains("workflow_id") || workflow.value("workflow_id", "").empty()) {
        workflow["workflow_id"] = std::string(workspace::generate_uuid().c_str());
    }
    if (!workflow.contains("created_at") || workflow.value("created_at", "").empty()) workflow["created_at"] = now_iso();
    workflow["updated_at"] = now_iso();

    try {
        std::filesystem::create_directories(file_path_.parent_path());
        std::lock_guard<std::mutex> lock(runtime_workflow_file_mutex());
        std::ofstream out(file_path_, std::ios::app | std::ios::binary);
        if (!out) return Json{{"success", false}, {"error_type", "runtime_workflow_open_failed"}, {"message", "failed to open runtime workflow log"}};
        out << workflow.dump().to_std_string() << '\n';
        return Json{{"success", true}, {"workflow", workflow}};
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeWorkflowStore append failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_workflow_append_failed"}, {"message", e.what()}};
    }
}

Json RuntimeWorkflowStore::get(const container::String& workflow_id) const {
    Json latest;
    try {
        std::lock_guard<std::mutex> lock(runtime_workflow_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", false}, {"error_type", "workflow_not_found"}, {"message", "workflow not found"}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto workflow = Json::parse(line);
                if (!workflow.is_object()) continue;
                if (workflow.value("workflow_id", "") == as_string(workflow_id)) latest = std::move(workflow);
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeWorkflowStore get failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_workflow_read_failed"}, {"message", e.what()}};
    }
    if (latest.is_null()) return Json{{"success", false}, {"error_type", "workflow_not_found"}, {"message", "workflow not found"}};
    return Json{{"success", true}, {"workflow", latest}};
}

Json RuntimeWorkflowStore::list(const RuntimeWorkflowQuery& query) const {
    std::vector<Json> ordered;
    std::vector<std::string> ids;
    try {
        std::lock_guard<std::mutex> lock(runtime_workflow_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", true}, {"workflows", Json::array()}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto workflow = Json::parse(line);
                if (!workflow.is_object()) continue;
                auto id = workflow.value("workflow_id", "");
                auto found = std::find(ids.begin(), ids.end(), id);
                if (found == ids.end()) {
                    ids.push_back(id);
                    ordered.push_back(std::move(workflow));
                } else {
                    ordered[static_cast<size_t>(std::distance(ids.begin(), found))] = std::move(workflow);
                }
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeWorkflowStore list failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_workflow_read_failed"}, {"message", e.what()}, {"workflows", Json::array()}};
    }

    Json workflows = Json::array();
    auto limit = query.limit > 0 ? query.limit : 100;
    for (auto it = ordered.rbegin(); it != ordered.rend() && workflows.size() < static_cast<size_t>(limit); ++it) {
        if (!matches_field(*it, "workspace", query.workspace)) continue;
        if (!matches_field(*it, "session_id", query.session_id)) continue;
        if (!matches_field(*it, "username", query.username)) continue;
        if (!matches_field(*it, "status", query.status)) continue;
        if (!matches_workflow_source(*it, query.source_execution_id)) continue;
        workflows.push_back(*it);
    }
    return Json{{"success", true}, {"workflows", workflows}};
}

Json RuntimeWorkflowStore::update(const container::String& workflow_id, Json patch) const {
    auto current = get(workflow_id);
    if (!current.value("success", false)) return current;
    auto workflow = current.value("workflow", Json::object());
    if (patch.is_object()) {
        for (auto it = patch.begin(); it != patch.end(); ++it) workflow[it.key()] = it.value();
    }
    workflow["workflow_id"] = as_string(workflow_id);
    workflow["updated_at"] = now_iso();
    return append(std::move(workflow));
}

RuntimeExecutionLinkStore::RuntimeExecutionLinkStore(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

Json RuntimeExecutionLinkStore::append(Json link) const {
    if (!link.is_object()) link = Json::object();
    if (!link.contains("link_id") || link.value("link_id", "").empty()) {
        link["link_id"] = std::string(workspace::generate_uuid().c_str());
    }
    if (!link.contains("ts") || link.value("ts", "").empty()) link["ts"] = now_iso();

    try {
        std::filesystem::create_directories(file_path_.parent_path());
        std::lock_guard<std::mutex> lock(runtime_execution_link_file_mutex());
        std::ofstream out(file_path_, std::ios::app | std::ios::binary);
        if (!out) return Json{{"success", false}, {"error_type", "runtime_link_open_failed"}, {"message", "failed to open runtime link log"}};
        out << link.dump().to_std_string() << '\n';
        return Json{{"success", true}, {"link", link}};
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeExecutionLinkStore append failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_link_append_failed"}, {"message", e.what()}};
    }
}

Json RuntimeExecutionLinkStore::list(const RuntimeExecutionLinkQuery& query) const {
    std::vector<Json> matched;
    try {
        std::lock_guard<std::mutex> lock(runtime_execution_link_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", true}, {"links", Json::array()}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto link = Json::parse(line);
                if (!link.is_object()) continue;
                if (!matches_field(link, "workspace", query.workspace)) continue;
                if (!matches_field(link, "session_id", query.session_id)) continue;
                if (!matches_field(link, "username", query.username)) continue;
                if (!matches_field(link, "relation", query.relation)) continue;
                if (!matches_link_execution(link, query.execution_id)) continue;
                matched.push_back(std::move(link));
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeExecutionLinkStore list failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_link_read_failed"}, {"message", e.what()}, {"links", Json::array()}};
    }

    Json links = Json::array();
    auto limit = query.limit > 0 ? query.limit : 100;
    for (auto it = matched.rbegin(); it != matched.rend() && links.size() < static_cast<size_t>(limit); ++it) {
        links.push_back(*it);
    }
    return Json{{"success", true}, {"links", links}};
}

RuntimeExecutionStore::RuntimeExecutionStore(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

Json RuntimeExecutionStore::append(Json execution) const {
    if (!execution.is_object()) execution = Json::object();
    if (!execution.contains("execution_id") || execution.value("execution_id", "").empty()) {
        execution["execution_id"] = std::string(workspace::generate_uuid().c_str());
    }
    if (!execution.contains("ts") || execution.value("ts", "").empty()) execution["ts"] = now_iso();

    try {
        std::filesystem::create_directories(file_path_.parent_path());
        std::lock_guard<std::mutex> lock(runtime_execution_file_mutex());
        std::ofstream out(file_path_, std::ios::app | std::ios::binary);
        if (!out) return Json{{"success", false}, {"error_type", "runtime_execution_open_failed"}, {"message", "failed to open runtime execution log"}};
        out << execution.dump().to_std_string() << '\n';
        return Json{{"success", true}, {"execution", execution}};
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeExecutionStore append failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_execution_append_failed"}, {"message", e.what()}};
    }
}

Json RuntimeExecutionStore::list(const RuntimeExecutionQuery& query) const {
    std::vector<Json> matched;
    try {
        std::lock_guard<std::mutex> lock(runtime_execution_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", true}, {"executions", Json::array()}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto execution = Json::parse(line);
                if (!execution.is_object()) continue;
                if (!matches_field(execution, "workspace", query.workspace)) continue;
                if (!matches_field(execution, "session_id", query.session_id)) continue;
                if (!matches_field(execution, "username", query.username)) continue;
                if (!matches_field(execution, "action", query.action)) continue;
                if (!matches_field(execution, "status", query.status)) continue;
                if (!matches_operation_capability(execution, query.capability)) continue;
                matched.push_back(std::move(execution));
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeExecutionStore list failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_execution_read_failed"}, {"message", e.what()}, {"executions", Json::array()}};
    }

    Json executions = Json::array();
    auto limit = query.limit > 0 ? query.limit : 100;
    for (auto it = matched.rbegin(); it != matched.rend() && executions.size() < static_cast<size_t>(limit); ++it) {
        executions.push_back(*it);
    }
    return Json{{"success", true}, {"executions", executions}};
}

Json RuntimeExecutionStore::get(const container::String& execution_id) const {
    try {
        std::lock_guard<std::mutex> lock(runtime_execution_file_mutex());
        std::ifstream in(file_path_, std::ios::binary);
        if (!in) return Json{{"success", false}, {"error_type", "execution_not_found"}, {"message", "execution not found"}};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto execution = Json::parse(line);
                if (!execution.is_object()) continue;
                if (execution.value("execution_id", "") == as_string(execution_id)) {
                    return Json{{"success", true}, {"execution", execution}};
                }
            } catch (...) {
            }
        }
        return Json{{"success", false}, {"error_type", "execution_not_found"}, {"message", "execution not found"}};
    } catch (const std::exception& e) {
        log::error_fmt("RuntimeExecutionStore get failed: {}", e.what());
        return Json{{"success", false}, {"error_type", "runtime_execution_read_failed"}, {"message", e.what()}};
    }
}

} // namespace ben_gear::audit
