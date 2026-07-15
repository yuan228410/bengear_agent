#include "capabilities/tool/permission_tools.hpp"

#include "capabilities/permission/policy_engine.hpp"
#include "capabilities/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

void register_permission_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<permission::PolicyEngine> engine) {
    if (!engine) return;

    registry.register_tool(
        std::string("list_pending_permissions"),
        std::string("List pending permission requests. Read-only."),
        {},
        [engine](const Json&) -> std::string {
            auto result = engine->list_pending().dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("approve_permission"),
        std::string("Approve a pending permission request. Optionally allow the same policy for the session."),
        {{std::string("permission_id"), {std::string("string"), std::string("Permission id returned by permission_required"), {}, true}},
         {std::string("allow_session"), {std::string("boolean"), std::string("Allow the same policy for the rest of the session"), {}, false}}},
        [engine](const Json& args) -> std::string {
            auto permission_id = args.value("permission_id", "");
            bool allow_session = args.value("allow_session", false);
            auto result = engine->approve(permission_id, allow_session).dump();
            return std::string(result.c_str(), result.size());
        });

    registry.register_tool(
        std::string("deny_permission"),
        std::string("Deny and remove a pending permission request."),
        {{std::string("permission_id"), {std::string("string"), std::string("Permission id returned by permission_required"), {}, true}}},
        [engine](const Json& args) -> std::string {
            auto permission_id = args.value("permission_id", "");
            auto result = engine->deny_pending(permission_id).dump();
            return std::string(result.c_str(), result.size());
        });
}

} // namespace ben_gear::tools
