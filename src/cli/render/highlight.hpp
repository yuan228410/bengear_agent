#pragma once

#include "cli/render/theme.hpp"
#include "base/container/map.hpp"
#include "cli/render/terminal.hpp"
#include "base/container/string.hpp"

#include <regex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ben_gear::cli {

namespace container = base::container;

/// 高亮 Token 类型
enum class HighlightToken : uint8_t {
    keyword,
    string_,
    comment,
    number,
    function_,
    type_,
};

/// 语法高亮规则
struct HighlightRule {
    container::String pattern;   // 正则表达式
    HighlightToken token;
};

/// 语言定义
struct LanguageDef {
    container::String name;
    std::vector<HighlightRule> rules;
    std::vector<container::String> keywords;
    container::String single_line_comment;  // 如 "//" 或 "#"
    container::String multi_comment_start;  // 如 "/*"
    container::String multi_comment_end;    // 如 "*/"
    container::String string_delimiters;    // 如 "\"'"
};

/// 语法高亮器
///
/// 高性能设计：
/// - 语言规则注册时预编译正则，运行时零编译开销
/// - 缓存编译后的 regex 对象，避免重复构造
/// - 按行处理，无状态机开销
class SyntaxHighlighter {
public:
    explicit SyntaxHighlighter(const Theme& theme, const TerminalCapabilities& cap);

    /// 对一行代码着色，返回 ANSI 字符串
    container::String highlight(std::string_view code, std::string_view lang) const;

    /// 是否支持某种语言
    bool supports(std::string_view lang) const;

    /// 注册自定义语言
    void register_language(const LanguageDef& def);

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;

    struct CompiledRule {
        std::regex pattern;
        HighlightToken token;
    };

    struct CompiledLanguage {
        container::String name;
        std::vector<CompiledRule> rules;
        std::vector<container::String> keywords;
        container::String single_line_comment;
        container::String multi_comment_start;
        container::String multi_comment_end;
        container::String string_delimiters;
    };

    base::container::Map<std::string, CompiledLanguage> compiled_;

    /// 获取 Token 对应的颜色
    const Color& token_color(HighlightToken token) const;

    /// 高亮一行代码
    container::String highlight_line(std::string_view line, const CompiledLanguage& lang) const;

    /// 注册内置语言规则
    void register_builtin_languages();
};

}  // namespace ben_gear::cli
