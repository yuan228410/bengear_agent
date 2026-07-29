#pragma once
#include "server/core/router.hpp"
#include "workspace/history_db.hpp"

#include <memory>

namespace ben_gear::server { class SessionPool; }

namespace ben_gear::server {

/// 会话检查 API — 查看系统提示词和上下文
/// GET /api/inspect/prompt?session_id=&workspace=    — 系统提示词（仅活跃会话）
/// GET /api/inspect/context?session_id=&workspace=  — 完整上下文（活跃优先，回退 DB）
void register_inspect_routes(Router& router, SessionPool& session_pool,
                              std::shared_ptr<workspace::HistoryDB> history_db);

} // namespace ben_gear::server
