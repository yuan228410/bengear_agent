#pragma once

#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ben_gear::cli {


/// 高亮 Token 类型
enum class HighlightToken : uint8_t {
    keyword,
    string_,
    comment,
    number,
    function_,
    type_,
};

/// 语言定义（纯数据，无正则）
struct LanguageDef {
    std::string name;
    std::vector<std::string> keywords;        // 关键字列表
    std::vector<std::string> types;           // 类型名列表（独立着色）
    std::string single_line_comment;           // 如 "//" 或 "#"
    std::string multi_comment_start;           // 如 "/*"
    std::string multi_comment_end;             // 如 "*/"
    std::string string_delimiters;             // 如 "\"'" 或 "\"'`"
};

/// 语法高亮器（手写字符扫描，O(n) 单遍，零分配）
///
/// 对比 std::regex 方案：
/// - 无 regex_search / smatch 堆分配
/// - 无 line → std::string 拷贝
/// - 单遍扫描，每字符 O(1)
/// - 支持跨行多行注释状态追踪
class SyntaxHighlighter {
public:
    explicit SyntaxHighlighter(const Theme& theme, const TerminalCapabilities& cap);

    /// 对一行代码着色，返回 ANSI 字符串
    std::string highlight(std::string_view code, std::string_view lang) const;

    /// 是否支持某种语言
    bool supports(std::string_view lang) const;

    /// 注册自定义语言
    void register_language(const LanguageDef& def);

    /// 重置跨行状态（文档结束时调用）
    void reset() const { in_multi_comment_ = false; }

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;

    struct CompiledLanguage {
        std::string name;
        std::vector<std::string> keywords;
        std::vector<std::string> types;
        std::string single_line_comment;
        std::string multi_comment_start;
        std::string multi_comment_end;
        std::string string_delimiters;
    };

    std::unordered_map<std::string, CompiledLanguage> compiled_;

    /// 跨行多行注释状态（mutable 因为 highlight 是 const）
    mutable bool in_multi_comment_{false};

    /// 获取 Token 对应的颜色
    const Color& token_color(HighlightToken token) const;

    /// 手写扫描器：逐字符扫描一行，返回 ANSI 着色结果
    std::string highlight_line(std::string_view line, const CompiledLanguage& lang) const;

    /// 输出带 ANSI 着色的文本片段
    void append_colored(std::string& out, std::string_view text, const Color& color) const;

    /// 注册内置语言规则
    void register_builtin_languages();
};

}  // namespace ben_gear::cli
