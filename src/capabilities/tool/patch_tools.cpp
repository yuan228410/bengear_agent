#include "capabilities/tool/patch_tools.hpp"

#include "application/patch_use_cases.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "capabilities/tool/registry.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

namespace container = base::container;

namespace detail {

Json app_error_to_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(error.details_json);
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", error.code},
                {"message", error.message}};
}

std::string json_tool_output(const Json& json) {
    auto dumped = json.dump();
    return std::string(dumped.c_str(), dumped.size());
}

} // namespace detail

void register_patch_tools(llm::ToolRegistry& registry,
                                 std::shared_ptr<patch::PatchService> service,
                                 std::shared_ptr<application::PatchUseCases> use_cases,
                                 application::RequestContext request) {
    if (!service) return;
    registry.register_tool(
        std::string("preview_diff"),
        std::string("Preview a unified diff without modifying files. Returns structured file changes and whether it can be applied."),
        {{std::string("unified_diff"), {std::string("string"), std::string("Unified diff text to preview"), {}, true}}},
        [service](const Json& args) -> std::string {
            auto diff = args.value("unified_diff", "");
            return detail::json_tool_output(patch::to_json(service->preview(diff)));
        },
        true);

    registry.register_tool(
        std::string("apply_patch"),
        std::string("Apply a unified diff to workspace files. Prefer this for code edits because it records a reversible change_id."),
        {{std::string("unified_diff"), {std::string("string"), std::string("Unified diff text to apply"), {}, true}},
         {std::string("description"), {std::string("string"), std::string("Short reason for the change"), {}, false}}},
        [use_cases, request](const Json& args) -> std::string {
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
        std::string("list_changes"),
        std::string("List patch changes recorded for the current session. Read-only."),
        {},
        [service](const Json&) -> std::string {
            auto result = service->list_changes();
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        },
        true);

    registry.register_tool(
        std::string("read_change"),
        std::string("Read a patch change record by change_id. Read-only."),
        {{std::string("change_id"), {std::string("string"), std::string("Change id returned by apply_patch"), {}, true}}},
        [service](const Json& args) -> std::string {
            auto change_id = args.value("change_id", "");
            auto result = service->read_change(change_id);
            if (!result.ok()) return detail::json_tool_output(detail::app_error_to_json(result.error()));
            return detail::json_tool_output(patch::to_json(result.value()));
        },
        true);

    registry.register_tool(
        std::string("revert_patch"),
        std::string("Revert a previously applied patch by change_id."),
        {{std::string("change_id"), {std::string("string"), std::string("Change id returned by apply_patch"), {}, true}},
         {std::string("force"), {std::string("boolean"), std::string("Force revert even if files changed after application"), {}, false}}},
        [use_cases, request](const Json& args) -> std::string {
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
