#pragma once
#include "server/core/router.hpp"
#include "workspace/history_db.hpp"
#include <memory>
#include <filesystem>

namespace ben_gear::server {

/// 数据库查看 API
/// GET /api/db/info          — 数据库路径、大小、表列表
/// GET /api/db/table/:name   — 表结构 + 分页数据（?page=1&limit=50）
void register_db_routes(Router& router, std::shared_ptr<workspace::HistoryDB> db);

} // namespace ben_gear::server
