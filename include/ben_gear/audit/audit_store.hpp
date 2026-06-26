#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <filesystem>

namespace ben_gear::audit {

namespace container = base::container;

struct AuditQuery {
    container::String workspace;
    container::String session_id;
    container::String category;
    container::String action;
    int limit = 100;
};

struct RuntimeExecutionQuery {
    container::String workspace;
    container::String session_id;
    container::String username;
    container::String action;
    container::String status;
    container::String capability;
    int limit = 100;
};

struct RuntimeExecutionLinkQuery {
    container::String workspace;
    container::String session_id;
    container::String username;
    container::String execution_id;
    container::String relation;
    int limit = 100;
};

struct RuntimeWorkflowQuery {
    container::String workspace;
    container::String session_id;
    container::String username;
    container::String status;
    container::String source_execution_id;
    int limit = 100;
};

class AuditStore {
public:
    explicit AuditStore(std::filesystem::path file_path);

    Json append(Json event) const;
    Json list(const AuditQuery& query) const;

private:
    std::filesystem::path file_path_;
};

class RuntimeWorkflowStore {
public:
    explicit RuntimeWorkflowStore(std::filesystem::path file_path);

    Json append(Json workflow) const;
    Json list(const RuntimeWorkflowQuery& query) const;
    Json get(const container::String& workflow_id) const;
    Json update(const container::String& workflow_id, Json patch) const;
    Json compact() const;

private:
    std::filesystem::path file_path_;
};

class RuntimeExecutionLinkStore {
public:
    explicit RuntimeExecutionLinkStore(std::filesystem::path file_path);

    Json append(Json link) const;
    Json list(const RuntimeExecutionLinkQuery& query) const;

private:
    std::filesystem::path file_path_;
};

class RuntimeExecutionStore {
public:
    explicit RuntimeExecutionStore(std::filesystem::path file_path);

    Json append(Json execution) const;
    Json list(const RuntimeExecutionQuery& query) const;
    Json get(const container::String& execution_id) const;

private:
    std::filesystem::path file_path_;
};

} // namespace ben_gear::audit
