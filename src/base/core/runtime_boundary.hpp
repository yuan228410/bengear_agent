#pragma once

#include "base/utils/json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ben_gear::base::core {

// Stable identifiers that may cross Core, Runtime, and UI adapters without
// pulling any concrete runtime implementation into the data model.
struct RequestContext {
    std::string request_id;
    std::string username;
    std::string workspace_name;
    std::string session_id = {};
};

struct WorkspaceRef {
    std::string username;
    std::string workspace_name;
    std::string project_path;
    std::string session_id;
};

enum class RuntimeCapability {
    tool_call,
    permission_gate,
    patch_preview,
    patch_apply,
    diff_read,
    git_status,
    git_commit,
    checkpoint_create,
    checkpoint_restore,
    test_loop,
    repo_map,
    code_intel,
};

enum class MutationScope {
    none,
    workspace_read,
    workspace_write,
    repository_write,
    external_effect,
};

struct RuntimeOperation {
    std::string operation_id;
    RuntimeCapability capability = RuntimeCapability::tool_call;
    MutationScope scope = MutationScope::none;
    WorkspaceRef workspace;
    std::string actor;
    std::string description;
};

struct ToolCallRef {
    std::string call_id;
    std::string tool_name;
    Json arguments = Json::object();
};

struct PermissionGateRef {
    std::string permission_id;
    std::string policy_key;
    MutationScope requested_scope = MutationScope::none;
    Json resource = Json::object();
};

struct PatchRef {
    std::string change_id;
    std::string description;
    int files_changed = 0;
    int additions = 0;
    int deletions = 0;
};

struct DiffRef {
    std::string path;
    int additions = 0;
    int deletions = 0;
};

struct GitRef {
    std::string repo_root;
    std::string branch;
    std::string commit;
    bool clean = true;
};

struct CheckpointRef {
    std::string checkpoint_id;
    std::string description;
    int files = 0;
};

struct RepoMapRef {
    std::string project_root;
    int indexed_files = 0;
    int total_symbols = 0;
};

struct RuntimeBoundary {
    RuntimeOperation operation;
    std::vector<ToolCallRef> tool_calls;
    std::vector<PermissionGateRef> permission_gates;
    std::vector<PatchRef> patches;
    std::vector<DiffRef> diffs;
    std::vector<GitRef> git_refs;
    std::vector<CheckpointRef> checkpoints;
    std::vector<RepoMapRef> repo_maps;
};

enum class RuntimeStatus {
    planned,
    running,
    succeeded,
    failed,
    skipped,
};

enum class RuntimeEventKind {
    state_changed,
    step_started,
    step_succeeded,
    step_failed,
    step_skipped,
    output_produced,
};

struct RuntimeEvent {
    std::string request_id;
    std::string operation_id;
    std::string step_id;
    RuntimeEventKind kind = RuntimeEventKind::state_changed;
    RuntimeStatus status = RuntimeStatus::planned;
    std::string message;
    Json details = Json::object();
};

using RuntimeEventSink = std::function<void(const RuntimeEvent&)>;

std::string to_string(RuntimeCapability capability);
std::string to_string(MutationScope scope);
std::string to_string(RuntimeStatus status);
std::string to_string(RuntimeEventKind kind);
Json to_json(const RequestContext& request);
Json to_json(const WorkspaceRef& workspace);
Json to_json(const RuntimeOperation& operation);
Json to_json(const ToolCallRef& ref);
Json to_json(const PermissionGateRef& ref);
Json to_json(const PatchRef& ref);
Json to_json(const DiffRef& ref);
Json to_json(const GitRef& ref);
Json to_json(const CheckpointRef& ref);
Json to_json(const RepoMapRef& ref);
Json to_json(const RuntimeBoundary& boundary);
Json to_json(const RuntimeEvent& event);

} // namespace ben_gear::base::core
