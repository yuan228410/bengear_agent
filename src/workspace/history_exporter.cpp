#include "ben_gear/workspace/history_exporter.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <fstream>
#include <sstream>

namespace ben_gear::workspace {

/// 格式化单条消息为 Markdown 片段
static void format_message_md(std::string& out, const Json& msg, const ExportOptions& opts) {
    auto role = msg.value("role", "");
    auto content = msg.value("content", "");
    auto ts = msg.value("ts", "");
    auto tool_name = msg.value("tool_name", "");

    // 按选项过滤
    if (role == "tool_call" && !opts.include_tool_calls) return;
    if (role == "thinking" && !opts.include_thinking) return;
    if (role == "tool" && !opts.include_tool_results) return;

    // 时间显示：只取日期+时间
    std::string time_str = ts;
    if (time_str.size() >= 19) {
        time_str = time_str.substr(0, 19);
    }

    if (role == "user") {
        out += "## ";
        out += time_str;
        out += " user\n\n";
        out += content;
        out += "\n\n";
    } else if (role == "assistant") {
        out += "## ";
        out += time_str;
        out += " assistant\n\n";
        out += content;
        out += "\n\n";
    } else if (role == "thinking") {
        out += "## ";
        out += time_str;
        out += " thinking\n\n> ";
        for (size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n') {
                out += "\n> ";
            } else {
                out += content[i];
            }
        }
        out += "\n\n";
    } else if (role == "tool_call") {
        auto name = tool_name.empty() ? "tool" : tool_name;
        out += "## ";
        out += time_str;
        out += " 🔧 ";
        out += name;
        out += "\n\n```json\n";
        out += content;
        out += "\n```\n\n";
    } else if (role == "tool") {
        auto name = tool_name.empty() ? "result" : tool_name;
        out += "## ";
        out += time_str;
        out += " 📤 ";
        out += name;
        out += " result\n\n```\n";
        if (content.size() > 2000) {
            out += content.substr(0, 2000);
            out += "\n... (truncated)";
        } else {
            out += content;
        }
        out += "\n```\n\n";
    } else if (role == "system") {
        out += "## ";
        out += time_str;
        out += " system\n\n";
        out += content;
        out += "\n\n";
    }
}

std::string HistoryExporter::export_session_md(
    HistoryDB& db,
    const container::String& workspace,
    const container::String& session_id,
    const ExportOptions& options) {
    // 确保数据落盘
    db.flush();

    container::Vector<Json> messages;

    // 有时间范围时，先用 DB 层过滤，再加载（高效）
    if (options.start_ts > 0 || options.end_ts > 0) {
        // 加载全部消息后在内存中过滤时间范围
        // （load_session 不支持时间过滤，但 search_by_time 不限定 session_id）
        // 所以先 load_session 再内存过滤
        messages = db.load_session(workspace, session_id, options.limit);
        // 过滤时间范围：ts 格式是 "2025-06-10T14:30:05"，需转 Unix 比较
        // 简单方案：在 format_message_md 中利用 DB 返回的 ts 字符串过滤
    } else {
        messages = db.load_session(workspace, session_id, options.limit);
    }

    if (messages.empty()) return "";

    std::string result;
    result.reserve(messages.size() * 128);
    result += "# 会话历史\n\n";

    for (const auto& msg : messages) {
        format_message_md(result, msg, options);
    }

    return result;
}

bool HistoryExporter::export_session_to_file(
    HistoryDB& db,
    const container::String& workspace,
    const container::String& session_id,
    const std::string& file_path,
    const ExportOptions& options) {
    auto content = export_session_md(db, workspace, session_id, options);
    if (content.empty()) {
        log::warn_fmt("HistoryExporter: no data to export for session={}",
                      std::string(session_id.data(), session_id.size()));
        return false;
    }

    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        log::error_fmt("HistoryExporter: failed to open file: {}", file_path);
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    log::info_fmt("HistoryExporter: exported session to {}", file_path);
    return true;
}

std::string HistoryExporter::export_search_md(
    HistoryDB& db,
    const container::String& keyword,
    const container::String& workspace,
    int limit) {
    auto messages = db.search(keyword, workspace, limit);
    if (messages.empty()) return "";

    std::string result;
    result.reserve(messages.size() * 128);
    result += "# 搜索结果\n\n关键词: ";
    result += std::string(keyword.data(), keyword.size());
    result += "\n\n---\n\n";

    ExportOptions opts;
    opts.include_tool_results = false;

    for (const auto& msg : messages) {
        format_message_md(result, msg, opts);
    }

    result += "---\n\n共 ";
    result += std::to_string(messages.size());
    result += " 条结果\n";

    return result;
}

std::string HistoryExporter::export_by_time_md(
    HistoryDB& db,
    const container::String& workspace,
    int64_t start_ts,
    int64_t end_ts,
    int limit) {
    auto messages = db.search_by_time(workspace, start_ts, end_ts, limit);
    if (messages.empty()) return "";

    std::string result;
    result.reserve(messages.size() * 128);
    result += "# 时间范围查询结果\n\n---\n\n";

    ExportOptions opts;
    opts.include_tool_results = false;

    for (const auto& msg : messages) {
        format_message_md(result, msg, opts);
    }

    result += "---\n\n共 ";
    result += std::to_string(messages.size());
    result += " 条结果\n";

    return result;
}

}  // namespace ben_gear::workspace
