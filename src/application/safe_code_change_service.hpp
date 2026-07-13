#pragma once

#include "application/command_pipeline.hpp"
#include "application/request_context.hpp"
#include "application/workspace_resolver.hpp"
#include "base/utils/json.hpp"
#include "capabilities/checkpoint/types.hpp"
#include "intelligence/code_intel/code_intelligence_index.hpp"
#include "domain/result.hpp"
#include "capabilities/git/types.hpp"
#include "capabilities/patch/types.hpp"
#include "capabilities/test_loop/types.hpp"

#include <memory>
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
                                   core::RuntimeEventSink event_sink = {},
                                   std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence = nullptr);

    domain::AppResult<SafeCodeChangeResult> run(const SafeCodeChangeCommand& command) const;

private:
    const WorkspaceResolver& workspace_resolver_;
    CommandPipeline command_pipeline_;
    core::RuntimeEventSink event_sink_;
    std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence_;
};

} // namespace ben_gear::application
