#pragma once

#include "base/utils/json.hpp"

#include <filesystem>
#include <mutex>

namespace ben_gear::audit {

namespace container = base::container;

struct AuditQuery {
    std::string workspace;
    std::string session_id;
    std::string category;
    std::string action;
    int limit = 100;
};

struct RuntimeExecutionQuery {
    std::string workspace;
    std::string session_id;
    std::string username;
    std::string action;
    std::string status;
    std::string capability;
    int limit = 100;
};

struct RuntimeExecutionLinkQuery {
    std::string workspace;
    std::string session_id;
    std::string username;
    std::string execution_id;
    std::string relation;
    int limit = 100;
};

struct RuntimeWorkflowQuery {
    std::string workspace;
    std::string session_id;
    std::string username;
    std::string status;
    std::string source_execution_id;
    int limit = 100;
};

class AuditStore {
public:
    explicit AuditStore(std::filesystem::path file_path);

    Json append(Json event) const;
    Json list(const AuditQuery& query) const;

private:
    std::filesystem::path file_path_;
    mutable std::mutex mutex_;
};

class RuntimeWorkflowStore {
public:
    explicit RuntimeWorkflowStore(std::filesystem::path file_path);

    Json append(Json workflow) const;
    Json list(const RuntimeWorkflowQuery& query) const;
    Json get(const std::string& workflow_id) const;
    Json update(const std::string& workflow_id, Json patch) const;
    Json compact() const;

private:
    std::filesystem::path file_path_;
    mutable std::mutex mutex_;
};

class RuntimeExecutionLinkStore {
public:
    explicit RuntimeExecutionLinkStore(std::filesystem::path file_path);

    Json append(Json link) const;
    Json list(const RuntimeExecutionLinkQuery& query) const;

private:
    std::filesystem::path file_path_;
    mutable std::mutex mutex_;
};

class RuntimeExecutionStore {
public:
    explicit RuntimeExecutionStore(std::filesystem::path file_path);

    Json append(Json execution) const;
    Json list(const RuntimeExecutionQuery& query) const;
    Json get(const std::string& execution_id) const;

private:
    std::filesystem::path file_path_;
    mutable std::mutex mutex_;
};

} // namespace ben_gear::audit
