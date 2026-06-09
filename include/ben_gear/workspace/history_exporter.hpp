#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <cstdint>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

class HistoryDB;

/// 历史消息导出选项（多界面通用，CLI/Web 共用）
struct ExportOptions {
    bool include_tool_calls = true;    // 包含工具调用
    bool include_thinking = true;      // 包含思考过程
    bool include_tool_results = false; // 包含工具返回结果（通常很长）
    int64_t start_ts = 0;             // 时间范围起始（Unix 秒，0=不限）
    int64_t end_ts = 0;               // 时间范围结束（0=不限）
    int limit = 0;                    // 消息条数限制（0=全部）
};

/// 历史消息导出器（格式化层，与界面解耦）
///
/// 职责：从 HistoryDB 读取数据，按选项格式化为 Markdown
/// 不依赖任何 UI 框架，CLI/Web/API 均可调用
class HistoryExporter {
public:
    /// 导出指定会话为 Markdown 字符串
    static std::string export_session_md(
        HistoryDB& db,
        const container::String& workspace,
        const container::String& session_id,
        const ExportOptions& options = {});

    /// 导出指定会话为 Markdown，直接写入文件
    static bool export_session_to_file(
        HistoryDB& db,
        const container::String& workspace,
        const container::String& session_id,
        const std::string& file_path,
        const ExportOptions& options = {});

    /// 导出搜索结果为 Markdown 字符串
    static std::string export_search_md(
        HistoryDB& db,
        const container::String& keyword,
        const container::String& workspace = {},
        int limit = 50);

    /// 导出时间范围内消息为 Markdown 字符串
    static std::string export_by_time_md(
        HistoryDB& db,
        const container::String& workspace,
        int64_t start_ts,
        int64_t end_ts,
        int limit = 100);
};

}  // namespace ben_gear::workspace
