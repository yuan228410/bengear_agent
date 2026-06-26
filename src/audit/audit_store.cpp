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
