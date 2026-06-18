#pragma once

#include "ben_gear/patch/patch_service.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

inline void register_patch_tools(llm::ToolRegistry& registry,
                                 std::shared_ptr<patch::PatchService> service) {
    if (!service) return;
    registry.register_tool(
        base::container::String("preview_diff"),
        base::container::String("Preview a unified diff without modifying files. Returns structured file changes and whether it can be applied."),
        {{base::container::String("unified_diff"), {base::container::String("string"), base::container::String("Unified diff text to preview"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto diff = args.value("unified_diff", "");
            auto result = patch::to_json(service->preview(diff)).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("apply_patch"),
        base::container::String("Apply a unified diff to workspace files. Prefer this for code edits because it records a reversible change_id."),
        {{base::container::String("unified_diff"), {base::container::String("string"), base::container::String("Unified diff text to apply"), {}, true}},
         {base::container::String("description"), {base::container::String("string"), base::container::String("Short reason for the change"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto diff = args.value("unified_diff", "");
            auto description = args.value("description", "");
            auto result = service->apply(diff, description).dump();
            return base::container::String(result.c_str(), result.size());
        });

    registry.register_tool(
        base::container::String("list_changes"),
        base::container::String("List patch changes recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> base::container::String {
            auto result = service->list_changes().dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("read_change"),
        base::container::String("Read a patch change record by change_id. Read-only."),
        {{base::container::String("change_id"), {base::container::String("string"), base::container::String("Change id returned by apply_patch"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto change_id = args.value("change_id", "");
            auto result = service->read_change(change_id).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("revert_patch"),
        base::container::String("Revert a previously applied patch by change_id."),
        {{base::container::String("change_id"), {base::container::String("string"), base::container::String("Change id returned by apply_patch"), {}, true}},
         {base::container::String("force"), {base::container::String("boolean"), base::container::String("Force revert even if files changed after application"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto change_id = args.value("change_id", "");
            bool force = args.value("force", false);
            auto result = service->revert(change_id, force).dump();
            return base::container::String(result.c_str(), result.size());
        });
}

} // namespace ben_gear::tools
