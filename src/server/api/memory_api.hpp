#pragma once
#include "server/core/router.hpp"
#include "workspace/resolver.hpp"

namespace ben_gear::server {

/// 记忆管理 API（三层级记忆文件 + 情景记忆）
/// GET    /api/memory/list                      — 记忆文件列表（三层级）
/// GET    /api/memory/read?tier=&kind=           — 读取记忆文件
/// POST   /api/memory/write                      — 写入记忆文件
/// DELETE /api/memory/delete?tier=&kind=          — 删除记忆文件
/// GET    /api/memory/episodes?workspace=         — 情景记忆日期列表
/// GET    /api/memory/episode/read?workspace=&date= — 读取情景记忆
/// POST   /api/memory/episode/write              — 写入情景记忆
/// DELETE /api/memory/episode/delete?workspace=&date= — 删除情景记忆
void register_memory_routes(Router& router, const workspace::WorkspaceResolver& resolver);

} // namespace ben_gear::server
