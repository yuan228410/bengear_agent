#pragma once

#include <vector>
#include "base/log/logger.hpp"
#include "workspace/manager.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"

#include <memory>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册工作空间管理工具
inline void register_workspace_tools(llm::ToolRegistry& tools,
                                      std::shared_ptr<workspace::WorkspaceManager> ws_manager) {
    if (!ws_manager) return;

    // list_workspaces
    tools.register_tool(
        std::string("list_workspaces"),
        std::string("List all workspaces for the current user"),
        {},
        [ws_manager](const Json& /*args*/) -> std::string {
            auto workspaces = ws_manager->list_all();
            std::string result;
            for (const auto& ws : workspaces) {
                result += "- " + std::string(ws.name.data(), ws.name.size());
                if (!std::string(ws.project_path.data(), ws.project_path.size()).empty()) {
                    result += " (" + std::string(ws.project_path.data(), ws.project_path.size()) + ")";
                }
                result += "\n";
            }
            if (result.empty()) return std::string("(no workspaces)");
            return result;
        }
    );

    // create_workspace
    tools.register_tool(
        std::string("create_workspace"),
        std::string("Create a new workspace"),
        {
            {"name", llm::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Workspace name")
            }},
            {"project_path", llm::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Associated project directory path (optional)")
            }},
        },
        [ws_manager](const Json& args) -> std::string {
            auto name = args.value("name", "");
            auto project_path = args.value("project_path", "");
            if (name.empty()) return std::string("Error: name is required");
            auto meta = ws_manager->create(
                name,
                project_path
            );
            if (meta) {
                return ("Workspace created: " + name);
            }
            return ("Workspace already exists: " + name);
        }
    );

    // remove_workspace
    tools.register_tool(
        std::string("remove_workspace"),
        std::string("Soft-delete a workspace (can be restored)"),
        {
            {"name", llm::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Workspace name to remove")
            }},
        },
        [ws_manager](const Json& args) -> std::string {
            auto name = args.value("name", "");
            if (name.empty()) return std::string("Error: name is required");
            if (ws_manager->remove(name)) {
                return ("Workspace removed: " + name);
            }
            return ("Failed to remove workspace: " + name);
        }
    );

    // restore_workspace
    tools.register_tool(
        std::string("restore_workspace"),
        std::string("Restore a previously removed workspace"),
        {
            {"name", llm::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Workspace name to restore")
            }},
        },
        [ws_manager](const Json& args) -> std::string {
            auto name = args.value("name", "");
            if (name.empty()) return std::string("Error: name is required");
            if (ws_manager->restore(name)) {
                return ("Workspace restored: " + name);
            }
            return ("Failed to restore workspace: " + name);
        }
    );

    log::info_fmt("registered workspace tools");
}

}  // namespace ben_gear::tools
