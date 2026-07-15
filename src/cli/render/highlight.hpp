#pragma once

#include "cli/render/theme.hpp"
#include <unordered_map>
#include "cli/render/terminal.hpp"

#include <regex>
#include <string_view>
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
    std::string pattern;   // 正则表达式
    HighlightToken token;
};

/// 语言定义
struct LanguageDef {
    std::string name;
    std::vector<HighlightRule> rules;
    std::vector<std::string> keywords;
    std::string single_line_comment;  // 如 "//" 或 "#"
    std::string multi_comment_start;  // 如 "/*"
    std::string multi_comment_end;    // 如 "*/"
    std::string string_delimiters;    // 如 "\"'"
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
    std::string highlight(std::string_view code, std::string_view lang) const;

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
        std::string name;
        std::vector<CompiledRule> rules;
        std::vector<std::string> keywords;
        std::string single_line_comment;
        std::string multi_comment_start;
        std::string multi_comment_end;
        std::string string_delimiters;
    };

    std::unordered_map<std::string, CompiledLanguage> compiled_;

    /// 获取 Token 对应的颜色
    const Color& token_color(HighlightToken token) const;

    /// 高亮一行代码
    std::string highlight_line(std::string_view line, const CompiledLanguage& lang) const;

    /// 注册内置语言规则
    void register_builtin_languages();
};

}  // namespace ben_gear::cli
