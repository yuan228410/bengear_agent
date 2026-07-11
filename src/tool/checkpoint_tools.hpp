#pragma once

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>
#include <vector>

namespace ben_gear::tools {

inline void register_checkpoint_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<checkpoint::CheckpointService> service,
                                      application::CommandPipeline command_pipeline = application::CommandPipeline(),
                                      application::RequestContext request = {},
                                      base::container::String project_path = base::container::String()) {
    if (!service) return;

    registry.register_tool(
        base::container::String("create_checkpoint"),
        base::container::String("Create a reversible checkpoint for selected workspace files before editing."),
        {{base::container::String("paths"), {base::container::String("array"), base::container::String("Relative file paths to snapshot; must be non-empty"), {}, true}},
         {base::container::String("description"), {base::container::String("string"), base::container::String("Short reason for the checkpoint"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            auto description = args.value("description", "");
            auto result = command_detail::app_result_json(service->create(paths, description), [](const checkpoint::CheckpointCreateResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return base::container::String(result.c_str(), result.size());
        });

    registry.register_tool(
        base::container::String("list_checkpoints"),
        base::container::String("List checkpoints recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> base::container::String {
            auto result = command_detail::app_result_json(service->list(), [](const checkpoint::CheckpointListResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("read_checkpoint"),
        base::container::String("Read a checkpoint record by checkpoint_id. Read-only."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto checkpoint_id = args.value("checkpoint_id", "");
            auto result = command_detail::app_result_json(service->read(checkpoint_id), [](const checkpoint::CheckpointReadResult& value) {
                return checkpoint::to_json(value);
            }).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("restore_checkpoint"),
        base::container::String("Restore files from a checkpoint. Mutating and permission-gated."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}},
         {base::container::String("paths"), {base::container::String("array"), base::container::String("Optional subset of paths to restore"), {}, false}},
         {base::container::String("force"), {base::container::String("boolean"), base::container::String("Force restore even if files changed after checkpoint"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
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
                        if (!file.path.empty()) command.affected_paths.push_back(base::container::String(file.path.c_str()));
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
        base::container::String("delete_checkpoint"),
        base::container::String("Delete a checkpoint record. Mutating and permission-gated."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
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
