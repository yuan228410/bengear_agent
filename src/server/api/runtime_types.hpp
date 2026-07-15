#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct RuntimeApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::string& action,
                       const std::string& status,
                       const std::string& capability,
                       int limit)> list_executions;
    std::function<Json(const std::string& username,
                       const std::string& execution_id)> read_execution;
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::string& execution_id,
                       const std::string& relation,
                       int limit)> list_links;
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::string& source_execution_id,
                       const Json& body)> append_link;
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::string& status,
                       const std::string& source_execution_id,
                       int limit)> list_workflows;
    std::function<Json(const std::string& username,
                       const std::string& workflow_id)> read_workflow;
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const Json& body)> start_repair_workflow;
    std::function<Json(const std::string& username,
                       const std::string& workflow_id,
                       const Json& body)> resume_workflow;
    std::function<Json(const std::string& username,
                       const std::string& workflow_id)> cancel_workflow;
    std::function<Json(const std::string& username,
                       const std::string& workflow_id)> workflow_timeline;
    std::function<Json(const std::string& username,
                       const std::string& workflow_id)> workflow_integrity;
    std::function<Json(const std::string& username)> compact_workflows;
};

} // namespace ben_gear::server
