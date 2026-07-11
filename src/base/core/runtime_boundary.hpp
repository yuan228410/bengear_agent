#pragma once

#include "base/container/string.hpp"
#include "base/utils/json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ben_gear::core {

namespace container = base::container;

// Stable identifiers that may cross Core, Runtime, and UI adapters without
// pulling any concrete runtime implementation into the data model.
struct RequestContext {
    container::String request_id;
    container::String username;
    container::String workspace_name;
    container::String session_id;
};

struct WorkspaceRef {
    container::String username;
    container::String workspace_name;
    container::String project_path;
    container::String session_id;
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
    container::String operation_id;
    RuntimeCapability capability = RuntimeCapability::tool_call;
    MutationScope scope = MutationScope::none;
    WorkspaceRef workspace;
    container::String actor;
    container::String description;
};

struct ToolCallRef {
    container::String call_id;
    container::String tool_name;
    Json arguments = Json::object();
};

struct PermissionGateRef {
    container::String permission_id;
    container::String policy_key;
    MutationScope requested_scope = MutationScope::none;
    Json resource = Json::object();
};

struct PatchRef {
    container::String change_id;
    container::String description;
    int files_changed = 0;
    int additions = 0;
    int deletions = 0;
};

struct DiffRef {
    container::String path;
    int additions = 0;
    int deletions = 0;
};

struct GitRef {
    container::String repo_root;
    container::String branch;
    container::String commit;
    bool clean = true;
};

struct CheckpointRef {
    container::String checkpoint_id;
    container::String description;
    int files = 0;
};

struct RepoMapRef {
    container::String project_root;
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
    container::String request_id;
    container::String operation_id;
    container::String step_id;
    RuntimeEventKind kind = RuntimeEventKind::state_changed;
    RuntimeStatus status = RuntimeStatus::planned;
    container::String message;
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

} // namespace ben_gear::core
