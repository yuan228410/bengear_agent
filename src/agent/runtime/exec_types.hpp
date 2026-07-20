#pragma once

#include "base/utils/json.hpp"
#include "base/core/runtime_boundary.hpp"

#include <string>
#include <vector>

namespace ben_gear::agent::runtime {

// ─── Command risk ─────────────────────────────────────────────
enum class CommandRisk {
    read_only,
    workspace_read,
    workspace_write,
    command_execution,
    destructive
};

struct CommandDescriptor {
    std::string action;
    std::string username;
    std::string workspace_name;
    std::string session_id;
    std::string project_path;
    std::string subject;
    CommandRisk risk = CommandRisk::read_only;
    bool mutates_workspace = false;
    bool runs_command = false;
    bool force = false;
    bool staged = false;
    bool worktree = true;
    bool all = false;
    bool amend = false;
    int timeout_seconds = 0;
    int max_output_bytes = 0;
    std::string working_directory;
    std::vector<std::string> affected_paths;
};

// ─── Execution pipeline types ─────────────────────────────────
enum class ExecutionStepKind {
    validate,
    authorize,
    checkpoint,
    execute,
    audit,
};

enum class ExecutionStatus {
    planned,
    running,
    succeeded,
    failed,
    skipped,
};

struct ExecutionStep {
    std::string step_id;
    ExecutionStepKind kind = ExecutionStepKind::execute;
    std::string title;
    bool required = true;
    bool mutates_workspace = false;
    Json metadata = Json::object();
};

struct ExecutionPlan {
    std::string plan_id;
    core::RuntimeBoundary boundary;
    std::vector<ExecutionStep> steps;
    bool dry_run = false;
};

struct ExecutionTraceEvent {
    std::string step_id;
    ExecutionStepKind kind = ExecutionStepKind::execute;
    ExecutionStatus status = ExecutionStatus::planned;
    std::string error_type;
    std::string message;
    Json details = Json::object();
};

struct ExecutionRequest {
    std::string request_id;
    CommandDescriptor command;
    core::RuntimeBoundary boundary;
    bool dry_run = false;
};

struct ExecutionResult {
    std::string request_id;
    ExecutionStatus status = ExecutionStatus::planned;
    ExecutionPlan plan;
    std::vector<ExecutionTraceEvent> trace;
    Json output = Json::object();
};

// ─── to_string / to_json ──────────────────────────────────────
std::string to_string(ExecutionStepKind kind);
std::string to_string(ExecutionStatus status);
Json to_json(const ExecutionStep& step);
Json to_json(const ExecutionPlan& plan);
Json to_json(const ExecutionTraceEvent& event);
Json to_json(const ExecutionResult& result);

} // namespace ben_gear::agent::runtime
