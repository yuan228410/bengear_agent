#pragma once

#include "application/patch_use_cases.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "tool/registry.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

namespace container = base::container;

namespace detail {

inline Json app_error_to_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(std::string(error.details_json.c_str()));
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", std::string(error.code.c_str())},
                {"message", std::string(error.message.c_str())}};
}

inline container::String json_tool_output(const Json& json) {
    auto dumped = json.dump();
    return container::String(dumped.c_str(), dumped.size());
}

} // namespace detail

inline void register_patch_tools(llm::ToolRegistry& registry,
                                 std::shared_ptr<patch::PatchService> service,
                                 std::shared_ptr<application::PatchUseCases> use_cases,
                                 application::RequestContext request) {
    if (!service) return;
    registry.register_tool(
        container::String("preview_diff"),
        container::String("Preview a unified diff without modifying files. Returns structured file changes and whether it can be applied."),
        {{container::String("unified_diff"), {container::String("string"), container::String("Unified diff text to preview"), {}, true}}},
        [service](const Json& args) -> container::String {
            auto diff = args.value("unified_diff", "");
            return detail::json_tool_output(patch::to_json(service->preview(diff)));
        },
        true);

    registry.register_tool(
        container::String("apply_patch"),
        container::String("Apply a unified diff to workspace files. Prefer this for code edits because it records a reversible change_id."),
        {{container::String("unified_diff"), {container::String("string"), container::String("Unified diff text to apply"), {}, true}},
         {container::String("description"), {container::String("string"), container::String("Short reason for the change"), {}, false}}},
        [use_cases, request](const Json& args) -> container::String {
            if (!use_cases) {
                return detail::json_tool_output(Json{{"success", false}, {"error_type", "patch_use_cases_unavailable"}, {"message", "patch use cases unavailable"}});
            }
            application::PatchApplyCommand command;
            command.request = request;
            command.unified_diff = args.value("unified_diff", "");
            command.description = args.value("description", "");
            auto result = use_cases->apply_patch(command);
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        });

    registry.register_tool(
        container::String("list_changes"),
        container::String("List patch changes recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> container::String {
            auto result = service->list_changes();
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        },
        true);

    registry.register_tool(
        container::String("read_change"),
        container::String("Read a patch change record by change_id. Read-only."),
        {{container::String("change_id"), {container::String("string"), container::String("Change id returned by apply_patch"), {}, true}}},
        [service](const Json& args) -> container::String {
            auto change_id = args.value("change_id", "");
            auto result = service->read_change(change_id);
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        },
        true);

    registry.register_tool(
        container::String("revert_patch"),
        container::String("Revert a previously applied patch by change_id."),
        {{container::String("change_id"), {container::String("string"), container::String("Change id returned by apply_patch"), {}, true}},
         {container::String("force"), {container::String("boolean"), container::String("Force revert even if files changed after application"), {}, false}}},
        [use_cases, request](const Json& args) -> container::String {
            if (!use_cases) {
                return detail::json_tool_output(Json{{"success", false}, {"error_type", "patch_use_cases_unavailable"}, {"message", "patch use cases unavailable"}});
            }
            application::PatchRevertCommand command;
            command.request = request;
            command.change_id = args.value("change_id", "");
            command.force = args.value("force", false);
            auto result = use_cases->revert_patch(command);
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        });
}

} // namespace ben_gear::tools
