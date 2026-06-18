#pragma once

#include "ben_gear/checkpoint/checkpoint_service.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>
#include <vector>

namespace ben_gear::tools {

inline void register_checkpoint_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<checkpoint::CheckpointService> service) {
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
            auto result = service->create(paths, description).dump();
            return base::container::String(result.c_str(), result.size());
        });

    registry.register_tool(
        base::container::String("list_checkpoints"),
        base::container::String("List checkpoints recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> base::container::String {
            auto result = service->list().dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("read_checkpoint"),
        base::container::String("Read a checkpoint record by checkpoint_id. Read-only."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto checkpoint_id = args.value("checkpoint_id", "");
            auto result = service->read(checkpoint_id).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("restore_checkpoint"),
        base::container::String("Restore files from a checkpoint. Mutating and permission-gated."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}},
         {base::container::String("paths"), {base::container::String("array"), base::container::String("Optional subset of paths to restore"), {}, false}},
         {base::container::String("force"), {base::container::String("boolean"), base::container::String("Force restore even if files changed after checkpoint"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            auto checkpoint_id = args.value("checkpoint_id", "");
            bool force = args.value("force", false);
            auto result = service->restore(checkpoint_id, paths, force).dump();
            return base::container::String(result.c_str(), result.size());
        });

    registry.register_tool(
        base::container::String("delete_checkpoint"),
        base::container::String("Delete a checkpoint record. Mutating and permission-gated."),
        {{base::container::String("checkpoint_id"), {base::container::String("string"), base::container::String("Checkpoint id returned by create_checkpoint"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto checkpoint_id = args.value("checkpoint_id", "");
            auto result = service->remove(checkpoint_id).dump();
            return base::container::String(result.c_str(), result.size());
        });
}

} // namespace ben_gear::tools
