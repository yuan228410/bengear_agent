#include "ben_gear/application/command_pipeline.hpp"

namespace ben_gear::application {

CommandPipeline::CommandPipeline(CommandPipelineHooks hooks)
    : hooks_(std::move(hooks)) {}

domain::AppResult<void> CommandPipeline::run_stage(
    const std::function<domain::AppResult<void>(const CommandDescriptor&)>& stage,
    const CommandDescriptor& descriptor) {
    if (!stage) return domain::AppResult<void>::success();
    return stage(descriptor);
}

void CommandPipeline::audit(const CommandDescriptor& descriptor, const domain::AppError* error) const {
    if (!hooks_.audit) return;
    hooks_.audit(descriptor, error);
}

} // namespace ben_gear::application
