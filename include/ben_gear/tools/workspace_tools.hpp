#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"

#include <memory>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册工作空间管理工具
inline void register_workspace_tools(llm::ToolRegistry& tools,
                                      std::shared_ptr<workspace::WorkspaceManager> ws_manager) {
    if (!ws_manager) return;

    // list_workspaces
    tools.register_tool(
        container::String("list_workspaces"),
        container::String("List all workspaces for the current user"),
        {},
        [ws_manager](const Json& /*args*/) -> container::String {
            auto workspaces = ws_manager->list_all();
            std::string result;
            for (const auto& ws : workspaces) {
                result += "- " + std::string(ws.name.data(), ws.name.size());
                if (!std::string(ws.project_path.data(), ws.project_path.size()).empty()) {
                    result += " (" + std::string(ws.project_path.data(), ws.project_path.size()) + ")";
                }
                result += "\n";
            }
            if (result.empty()) return container::String("(no workspaces)");
            return container::String(result.c_str());
        }
    );

    // create_workspace
    tools.register_tool(
        container::String("create_workspace"),
        container::String("Create a new workspace"),
        {
            {"name", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Workspace name")
            }},
            {"project_path", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Associated project directory path (optional)")
            }},
        },
        [ws_manager](const Json& args) -> container::String {
            auto name = args.value("name", "");
            auto project_path = args.value("project_path", "");
            if (name.empty()) return container::String("Error: name is required");
            auto meta = ws_manager->create(
                container::String(name.c_str()),
                container::String(project_path.c_str())
            );
            if (meta) {
                return container::String(("Workspace created: " + name).c_str());
            }
            return container::String(("Workspace already exists: " + name).c_str());
        }
    );

    // remove_workspace
    tools.register_tool(
        container::String("remove_workspace"),
        container::String("Soft-delete a workspace (can be restored)"),
        {
            {"name", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Workspace name to remove")
            }},
        },
        [ws_manager](const Json& args) -> container::String {
            auto name = args.value("name", "");
            if (name.empty()) return container::String("Error: name is required");
            if (ws_manager->remove(container::String(name.c_str()))) {
                return container::String(("Workspace removed: " + name).c_str());
            }
            return container::String(("Failed to remove workspace: " + name).c_str());
        }
    );

    // restore_workspace
    tools.register_tool(
        container::String("restore_workspace"),
        container::String("Restore a previously removed workspace"),
        {
            {"name", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Workspace name to restore")
            }},
        },
        [ws_manager](const Json& args) -> container::String {
            auto name = args.value("name", "");
            if (name.empty()) return container::String("Error: name is required");
            if (ws_manager->restore(container::String(name.c_str()))) {
                return container::String(("Workspace restored: " + name).c_str());
            }
            return container::String(("Failed to restore workspace: " + name).c_str());
        }
    );

    log::info_fmt("registered workspace tools");
}

}  // namespace ben_gear::tools
