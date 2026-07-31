#pragma once
#include "server/core/router.hpp"
#include "workspace/resolver.hpp"

#include <functional>

namespace ben_gear::server {

/// 配置编辑 API
/// GET  /api/config/raw    — 读取 config.json 原始内容
/// POST /api/config/save   — 保存 config.json（原子写入）+ 触发热重载
/// GET  /api/config/schema — 返回所有可配置项元数据（前端渲染用）
void register_config_edit_routes(Router& router,
                                  const workspace::WorkspaceResolver& resolver,
                                  std::function<bool()> reload_callback = {});

} // namespace ben_gear::server
