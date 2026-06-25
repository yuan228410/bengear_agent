#include "ben_gear/core/runtime_boundary.hpp"

namespace ben_gear::core {

std::string to_string(RuntimeCapability capability) {
    switch (capability) {
    case RuntimeCapability::tool_call: return "tool_call";
    case RuntimeCapability::permission_gate: return "permission_gate";
    case RuntimeCapability::patch_preview: return "patch_preview";
    case RuntimeCapability::patch_apply: return "patch_apply";
    case RuntimeCapability::diff_read: return "diff_read";
    case RuntimeCapability::git_status: return "git_status";
    case RuntimeCapability::git_commit: return "git_commit";
    case RuntimeCapability::checkpoint_create: return "checkpoint_create";
    case RuntimeCapability::checkpoint_restore: return "checkpoint_restore";
    case RuntimeCapability::test_loop: return "test_loop";
    case RuntimeCapability::repo_map: return "repo_map";
    case RuntimeCapability::code_intel: return "code_intel";
    }
    return "tool_call";
}

std::string to_string(MutationScope scope) {
    switch (scope) {
    case MutationScope::none: return "none";
    case MutationScope::workspace_read: return "workspace_read";
    case MutationScope::workspace_write: return "workspace_write";
    case MutationScope::repository_write: return "repository_write";
    case MutationScope::external_effect: return "external_effect";
    }
    return "none";
}

Json to_json(const RequestContext& request) {
    return Json{{"request_id", request.request_id.c_str()},
                {"username", request.username.c_str()},
                {"workspace_name", request.workspace_name.c_str()},
                {"session_id", request.session_id.c_str()}};
}

Json to_json(const WorkspaceRef& workspace) {
    return Json{{"username", workspace.username.c_str()},
                {"workspace_name", workspace.workspace_name.c_str()},
                {"project_path", workspace.project_path.c_str()},
                {"session_id", workspace.session_id.c_str()}};
}

Json to_json(const RuntimeOperation& operation) {
    return Json{{"operation_id", operation.operation_id.c_str()},
                {"capability", to_string(operation.capability)},
                {"scope", to_string(operation.scope)},
                {"workspace", to_json(operation.workspace)},
                {"actor", operation.actor.c_str()},
                {"description", operation.description.c_str()}};
}

Json to_json(const ToolCallRef& ref) {
    return Json{{"call_id", ref.call_id.c_str()}, {"tool_name", ref.tool_name.c_str()}, {"arguments", ref.arguments}};
}

Json to_json(const PermissionGateRef& ref) {
    return Json{{"permission_id", ref.permission_id.c_str()},
                {"policy_key", ref.policy_key.c_str()},
                {"requested_scope", to_string(ref.requested_scope)},
                {"resource", ref.resource}};
}

Json to_json(const PatchRef& ref) {
    return Json{{"change_id", ref.change_id.c_str()},
                {"description", ref.description.c_str()},
                {"files_changed", ref.files_changed},
                {"additions", ref.additions},
                {"deletions", ref.deletions}};
}

Json to_json(const DiffRef& ref) {
    return Json{{"path", ref.path.c_str()}, {"additions", ref.additions}, {"deletions", ref.deletions}};
}

Json to_json(const GitRef& ref) {
    return Json{{"repo_root", ref.repo_root.c_str()},
                {"branch", ref.branch.c_str()},
                {"commit", ref.commit.c_str()},
                {"clean", ref.clean}};
}

Json to_json(const CheckpointRef& ref) {
    return Json{{"checkpoint_id", ref.checkpoint_id.c_str()},
                {"description", ref.description.c_str()},
                {"files", ref.files}};
}

Json to_json(const RepoMapRef& ref) {
    return Json{{"project_root", ref.project_root.c_str()},
                {"indexed_files", ref.indexed_files},
                {"total_symbols", ref.total_symbols}};
}

Json to_json(const RuntimeBoundary& boundary) {
    Json tool_calls = Json::array();
    for (const auto& ref : boundary.tool_calls) tool_calls.push_back(to_json(ref));
    Json permission_gates = Json::array();
    for (const auto& ref : boundary.permission_gates) permission_gates.push_back(to_json(ref));
    Json patches = Json::array();
    for (const auto& ref : boundary.patches) patches.push_back(to_json(ref));
    Json diffs = Json::array();
    for (const auto& ref : boundary.diffs) diffs.push_back(to_json(ref));
    Json git_refs = Json::array();
    for (const auto& ref : boundary.git_refs) git_refs.push_back(to_json(ref));
    Json checkpoints = Json::array();
    for (const auto& ref : boundary.checkpoints) checkpoints.push_back(to_json(ref));
    Json repo_maps = Json::array();
    for (const auto& ref : boundary.repo_maps) repo_maps.push_back(to_json(ref));

    return Json{{"operation", to_json(boundary.operation)},
                {"tool_calls", tool_calls},
                {"permission_gates", permission_gates},
                {"patches", patches},
                {"diffs", diffs},
                {"git_refs", git_refs},
                {"checkpoints", checkpoints},
                {"repo_maps", repo_maps}};
}

} // namespace ben_gear::core
