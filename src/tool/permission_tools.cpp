#include "tool/permission_tools.hpp"

#include "capabilities/permission/policy_engine.hpp"
#include "tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

void register_permission_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<permission::PolicyEngine> engine) {
    if (!engine) return;

    registry.register_tool(
        base::container::String("list_pending_permissions"),
        base::container::String("List pending permission requests. Read-only."),
        {},
        [engine](const Json&) -> base::container::String {
            auto result = engine->list_pending().dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("approve_permission"),
        base::container::String("Approve a pending permission request. Optionally allow the same policy for the session."),
        {{base::container::String("permission_id"), {base::container::String("string"), base::container::String("Permission id returned by permission_required"), {}, true}},
         {base::container::String("allow_session"), {base::container::String("boolean"), base::container::String("Allow the same policy for the rest of the session"), {}, false}}},
        [engine](const Json& args) -> base::container::String {
            auto permission_id = args.value("permission_id", "");
            bool allow_session = args.value("allow_session", false);
            auto result = engine->approve(permission_id, allow_session).dump();
            return base::container::String(result.c_str(), result.size());
        });

    registry.register_tool(
        base::container::String("deny_permission"),
        base::container::String("Deny and remove a pending permission request."),
        {{base::container::String("permission_id"), {base::container::String("string"), base::container::String("Permission id returned by permission_required"), {}, true}}},
        [engine](const Json& args) -> base::container::String {
            auto permission_id = args.value("permission_id", "");
            auto result = engine->deny_pending(permission_id).dump();
            return base::container::String(result.c_str(), result.size());
        });
}

} // namespace ben_gear::tools
