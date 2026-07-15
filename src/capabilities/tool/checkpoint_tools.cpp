#include "capabilities/tool/checkpoint_tools.hpp"

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>
#include <vector>

namespace ben_gear::tools {

void register_checkpoint_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<checkpoint::CheckpointService> service,
                                      application::CommandPipeline command_pipeline,
                                      application::RequestContext request,
                                      std::string project_path) {
    if (!service) return;

    registry.register_tool(
        std::string("create_checkpoint"),
        std::string("Create a reversible checkpoint for selected workspace files before editing."),
        {{std::string("paths"), {std::string("array"), std::string("Relative file paths to snapshot; must be non-empty"), {}, true}},
         {std::string("description"), {std::string("string"), std::string("Short reason for the checkpoint"), {}, false}}},
        [service](const Json& args) -> std::string {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            auto description = args.value("description", "");
            auto result = command_detail::app_result_json(service->create(paths, description), [](const checkpoint::CheckpointCreateResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return std::string(result.c_str(), result.size());
        });

    registry.register_tool(
        std::string("list_checkpoints"),
        std::string("List checkpoints recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> std::string {
            auto result = command_detail::app_result_json(service->list(), [](const checkpoint::CheckpointListResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("read_checkpoint"),
        std::string("Read a checkpoint record by checkpoint_id. Read-only."),
        {{std::string("checkpoint_id"), {std::string("string"), std::string("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service](const Json& args) -> std::string {
            auto checkpoint_id = args.value("checkpoint_id", "");
            auto result = command_detail::app_result_json(service->read(checkpoint_id), [](const checkpoint::CheckpointReadResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("restore_checkpoint"),
        std::string("Restore files from a checkpoint. Mutating and permission-gated."),
        {{std::string("checkpoint_id"), {std::string("string"), std::string("Checkpoint id returned by create_checkpoint"), {}, true}},
         {std::string("paths"), {std::string("array"), std::string("Optional subset of paths to restore"), {}, false}},
         {std::string("force"), {std::string("boolean"), std::string("Force restore even if files changed after checkpoint"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            auto checkpoint_id = args.value("checkpoint_id", "");
            bool force = args.value("force", false);

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .checkpoint_restore(checkpoint_id, paths, force);
            if (command.affected_paths.empty()) {
                auto checkpoint = service->read(checkpoint_id);
                if (checkpoint.ok()) {
                    for (const auto& file : checkpoint.value().checkpoint.files) {
                        if (!file.path.empty()) command.affected_paths.push_back(file.path);
                    }
                }
            }

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                return command_detail::presented_command_result(service->restore(checkpoint_id, paths, force), [](const checkpoint::CheckpointRestoreResult& value) {
                    return checkpoint::to_json(value);
                });
            }));
        });

    registry.register_tool(
        std::string("delete_checkpoint"),
        std::string("Delete a checkpoint record. Mutating and permission-gated."),
        {{std::string("checkpoint_id"), {std::string("string"), std::string("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
            auto checkpoint_id = args.value("checkpoint_id", "");

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .checkpoint_delete(checkpoint_id);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                return command_detail::presented_command_result(service->remove(checkpoint_id), [](const checkpoint::CheckpointRemoveResult& value) {
                    return checkpoint::to_json(value);
                });
            }));
        });
}

} // namespace ben_gear::tools
