#pragma once

namespace ben_gear::capabilities::tool { class ToolRegistry; }
namespace ben_gear::base { class ServiceRegistry; }

namespace ben_gear::orchestration {

/// 注册 TODO 管理工具（update_todo）
void register_todo_tools(
    capabilities::tool::ToolRegistry& registry,
    base::ServiceRegistry& services);

} // namespace ben_gear::orchestration
