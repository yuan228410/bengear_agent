#pragma once

#include "base/container/string.hpp"
#include "base/container/vector.hpp"
#include "base/log/logger.hpp"
#include "workspace/history_db.hpp"
#include "workspace/types.hpp"
#include "tool/registry.hpp"
#include "tool/types.hpp"

#include <chrono>
#include <ctime>
#include <string>
#include <regex>
#include <set>

namespace ben_gear::tools {

namespace container = base::container;

/// 解析时间字符串为 Unix 时间戳（秒）
/// 支持：ISO 日期(2024-01-01)、ISO 日期时间(2024-01-01T12:30:00)、相对时间(7d/1h)
/// 返回 0 表示解析失败
int64_t parse_time_string(const std::string& time_str);


/// 将 container::String 转为 std::string
std::string to_std(const container::String& s);


/// 注册历史会话删除工具
void register_history_tools(llm::ToolRegistry& tools,
                                    workspace::HistoryDB& history_db,
                                    const workspace::WorkspaceContext& ws_ctx);


}  // namespace ben_gear::tools
