#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/utils/json.hpp"

namespace ben_gear::cli {

/// 显示配置 — 控制哪些内容要渲染
///
/// 纯数据结构，不含逻辑
/// 可从配置文件加载，也可从 CLI 参数覆盖
struct DisplayConfig {
    // ---- Thinking ----
    bool show_thinking = true;           // 是否显示思考过程
    bool show_thinking_label = true;      // thinking 起止标签

    // ---- 工具调用 ----
    bool show_tool_call = true;           // 是否显示工具调用
    bool show_tool_args = true;          // 是否显示工具参数（可能很大）
    bool show_tool_result = true;         // 是否显示工具结果
    int  tool_result_max_length = 200;    // 结果截断长度（0 = 不截断）
    bool show_tool_id = false;            // 是否显示 tool call id

    // ---- Markdown ----
    bool markdown_render = true;          // 是否渲染 Markdown
    bool syntax_highlight = true;         // 代码块语法高亮

    // ---- 交互 ----
    bool show_spinner = true;             // 等待时显示 Spinner
    bool show_timing = false;             // 是否显示耗时
    bool show_token_count = false;        // 是否显示 token 统计

    /// 从 JSON 配置合并（只覆盖存在的字段）
    static DisplayConfig from_json(const Json& j) {
        DisplayConfig cfg;
        if (j.contains("show_thinking")) cfg.show_thinking = j["show_thinking"].get<bool>();
        if (j.contains("show_thinking_label")) cfg.show_thinking_label = j["show_thinking_label"].get<bool>();
        if (j.contains("show_tool_call")) cfg.show_tool_call = j["show_tool_call"].get<bool>();
        if (j.contains("show_tool_args")) cfg.show_tool_args = j["show_tool_args"].get<bool>();
        if (j.contains("show_tool_result")) cfg.show_tool_result = j["show_tool_result"].get<bool>();
        if (j.contains("tool_result_max_length")) cfg.tool_result_max_length = j["tool_result_max_length"].get<int>();
        if (j.contains("show_tool_id")) cfg.show_tool_id = j["show_tool_id"].get<bool>();
        if (j.contains("markdown_render")) cfg.markdown_render = j["markdown_render"].get<bool>();
        if (j.contains("syntax_highlight")) cfg.syntax_highlight = j["syntax_highlight"].get<bool>();
        if (j.contains("show_spinner")) cfg.show_spinner = j["show_spinner"].get<bool>();
        if (j.contains("show_timing")) cfg.show_timing = j["show_timing"].get<bool>();
        if (j.contains("show_token_count")) cfg.show_token_count = j["show_token_count"].get<bool>();
        return cfg;
    }

    /// 转为 JSON
    Json to_json() const {
        return Json{
            {"show_thinking", show_thinking},
            {"show_thinking_label", show_thinking_label},
            {"show_tool_call", show_tool_call},
            {"show_tool_args", show_tool_args},
            {"show_tool_result", show_tool_result},
            {"tool_result_max_length", tool_result_max_length},
            {"show_tool_id", show_tool_id},
            {"markdown_render", markdown_render},
            {"syntax_highlight", syntax_highlight},
            {"show_spinner", show_spinner},
            {"show_timing", show_timing},
            {"show_token_count", show_token_count},
        };
    }
};

}  // namespace ben_gear::cli
