#pragma once

#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/application/request_context.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/checkpoint/types.hpp"
#include "ben_gear/domain/result.hpp"
#include "ben_gear/git/types.hpp"
#include "ben_gear/patch/types.hpp"
#include "ben_gear/test_loop/types.hpp"

#include <string>
#include <vector>

namespace ben_gear::application {

struct SafeCodeChangeCommand {
    RequestContext request;
    std::string unified_diff;
    std::string description;
    std::string test_command;
    std::string test_cwd = ".";
    int test_timeout_seconds = 120;
    int test_max_output_bytes = 60000;
};

struct SafeCodeChangeResult {
    bool success = false;
    std::string stage;
    std::string error_type;
    std::string message;
    patch::PatchPreview preview;
    checkpoint::CheckpointCreateResult checkpoint;
    patch::PatchApplyResult patch_apply;
    git::GitStatus git_status;
    git::GitDiffResult git_diff;
    test_loop::TestRunResult test_run;
    Json repo_intelligence = Json::object();
    std::string rollback_hint;
    Json execution = Json::object();
};

Json to_json(const SafeCodeChangeResult& result);

class SafeCodeChangeService {
public:
    explicit SafeCodeChangeService(const WorkspaceResolver& workspace_resolver,
                                   CommandPipeline command_pipeline = CommandPipeline(),
                                   core::RuntimeEventSink event_sink = {});

    domain::AppResult<SafeCodeChangeResult> run(const SafeCodeChangeCommand& command) const;

private:
    const WorkspaceResolver& workspace_resolver_;
    CommandPipeline command_pipeline_;
    core::RuntimeEventSink event_sink_;
};

} // namespace ben_gear::application
