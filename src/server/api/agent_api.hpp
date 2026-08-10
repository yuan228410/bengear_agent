#pragma once
#include "server/core/router.hpp"
#include "server/session/pool.hpp"

#include <memory>

namespace ben_gear::server {

/// Agent 列表 API — 供前端 @ 补全使用
/// GET /api/agents                — 返回所有可用 agent（内置 + 自定义 + team）
/// POST /api/agents/execute       — 直接执行模式（旁路主 Agent）
void register_agent_routes(Router& router, SessionPool& session_pool);

} // namespace ben_gear::server
