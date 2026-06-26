#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct RuntimeApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& action,
                       const container::String& status,
                       const container::String& capability,
                       int limit)> list_executions;
    std::function<Json(const container::String& username,
                       const container::String& execution_id)> read_execution;
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& execution_id,
                       const container::String& relation,
                       int limit)> list_links;
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& source_execution_id,
                       const Json& body)> append_link;
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& status,
                       const container::String& source_execution_id,
                       int limit)> list_workflows;
    std::function<Json(const container::String& username,
                       const container::String& workflow_id)> read_workflow;
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const Json& body)> start_repair_workflow;
    std::function<Json(const container::String& username,
                       const container::String& workflow_id,
                       const Json& body)> resume_workflow;
    std::function<Json(const container::String& username,
                       const container::String& workflow_id)> cancel_workflow;
};

} // namespace ben_gear::server
