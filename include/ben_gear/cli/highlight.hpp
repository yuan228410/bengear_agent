#pragma once

#include "ben_gear/cli/theme.hpp"
#include "ben_gear/cli/terminal.hpp"
#include "ben_gear/base/container/string.hpp"

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
    container::String string_delimiters;    // 如 "\"'" (双引号和单引号)
};

/// 语法高亮器
///
/// 高性能设计：
/// - 语言规则注册时预编译正则，运行时零编译开销
/// - 缓存编译后的 regex 对象，避免重复构造
/// - 按行处理，无状态机开销
class SyntaxHighlighter {
public:
    explicit SyntaxHighlighter(const Theme& theme, const TerminalCapabilities& cap)
        : theme_(theme), cap_(cap) {
        register_builtin_languages();
    }

    /// 对一行代码着色，返回 ANSI 字符串
    container::String highlight(std::string_view code, std::string_view lang) const {
        auto it = compiled_.find(std::string(lang));
        if (it == compiled_.end()) {
            // 未知语言，不着色
            return container::String(code);
        }
        return highlight_line(code, it->second);
    }

    /// 是否支持某种语言
    bool supports(std::string_view lang) const {
        return compiled_.find(std::string(lang)) != compiled_.end();
    }

    /// 注册自定义语言
    void register_language(const LanguageDef& def) {
        CompiledLanguage cl;
        cl.name = def.name;
        // 预编译所有正则
        for (const auto& rule : def.rules) {
            try {
                cl.rules.push_back({std::regex(def.name.empty() ? rule.pattern.c_str() : rule.pattern.c_str()), rule.token});
            } catch (...) {
                // 正则编译失败，跳过
            }
        }
        cl.keywords = def.keywords;
        cl.single_line_comment = def.single_line_comment;
        cl.multi_comment_start = def.multi_comment_start;
        cl.multi_comment_end = def.multi_comment_end;
        cl.string_delimiters = def.string_delimiters;
        compiled_.emplace(std::string(def.name.c_str()), std::move(cl));
    }

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

    std::unordered_map<std::string, CompiledLanguage> compiled_;

    /// 获取 Token 对应的颜色
    const Color& token_color(HighlightToken token) const {
        switch (token) {
            case HighlightToken::keyword:   return theme_.hl_keyword;
            case HighlightToken::string_:   return theme_.hl_string;
            case HighlightToken::comment:   return theme_.hl_comment;
            case HighlightToken::number:    return theme_.hl_number;
            case HighlightToken::function_: return theme_.hl_function;
            case HighlightToken::type_:     return theme_.hl_type;
        }
        return theme_.hl_keyword;
    }

    /// 高亮一行代码
    container::String highlight_line(std::string_view line, const CompiledLanguage& lang) const {
        if (!cap_.color || line.empty()) {
            return container::String(line);
        }

        // 使用简单高效的正则替换策略
        // 1. 先提取所有匹配区间
        // 2. 按位置排序
        // 3. 拼接结果

        struct Span {
            size_t start;
            size_t end;
            HighlightToken token;
        };

        std::vector<Span> spans;
        std::string line_str(line);

        // 遍历所有规则，提取匹配区间
        for (const auto& rule : lang.rules) {
            std::smatch match;
            std::string::const_iterator search_start(line_str.cbegin());
            while (std::regex_search(search_start, line_str.cend(), match, rule.pattern)) {
                size_t start = static_cast<size_t>(match[0].first - line_str.cbegin());
                size_t end = static_cast<size_t>(match[0].second - line_str.cbegin());
                spans.push_back({start, end, rule.token});
                search_start = match[0].second;
            }
        }

        // 无匹配，原样返回
        if (spans.empty()) {
            return container::String(line);
        }

        // 按起始位置排序
        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) {
            return a.start < b.start;
        });

        // 去重：后出现的 span 如果与前面的重叠，跳过
        std::vector<Span> merged;
        for (auto& span : spans) {
            bool overlap = false;
            for (auto& existing : merged) {
                if (span.start < existing.end && span.end > existing.start) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                merged.push_back(span);
            }
        }

        // 预估输出大小（原始长度 + ANSI 码开销）
        container::String result;
        result.reserve(line.size() + merged.size() * 40);

        size_t last_end = 0;
        for (const auto& span : merged) {
            // 未着色部分
            if (span.start > last_end) {
                result.append(line.data() + last_end, span.start - last_end);
            }
            // 着色部分
            auto color_code = ansi::fg(token_color(span.token), cap_);
            auto reset_code = ansi::reset();
            if (!color_code.empty()) result.append(color_code.data(), color_code.size());
            result.append(line.data() + span.start, span.end - span.start);
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            last_end = span.end;
        }
        // 尾部
        if (last_end < line.size()) {
            result.append(line.data() + last_end, line.size() - last_end);
        }

        return result;
    }

    /// 注册内置语言规则
    void register_builtin_languages() {
        // C/C++
        {
            LanguageDef def;
            def.name = "cpp";
            def.single_line_comment = "//";
            def.multi_comment_start = "/*";
            def.multi_comment_end = "*/";
            def.string_delimiters = "\"'";
            def.keywords = {"auto","break","case","catch","class","const","constexpr","continue",
                "default","delete","do","else","enum","explicit","extern","false","final",
                "for","friend","goto","if","inline","mutable","namespace","new","noexcept",
                "nullptr","operator","override","private","protected","public","register",
                "return","sizeof","static","static_assert","static_cast","struct","switch",
                "template","this","throw","true","try","typedef","typeid","typename","union",
                "using","virtual","volatile","while"};
            // 关键字
            def.rules.push_back({"\\b(auto|break|case|catch|class|const|constexpr|continue|default|delete|do|else|enum|explicit|extern|false|final|for|friend|goto|if|inline|mutable|namespace|new|noexcept|nullptr|operator|override|private|protected|public|register|return|sizeof|static|static_assert|static_cast|struct|switch|template|this|throw|true|try|typedef|typeid|typename|union|using|virtual|volatile|while)\\b", HighlightToken::keyword});
            // 类型
            def.rules.push_back({"\\b(int|long|short|float|double|char|void|bool|unsigned|signed|size_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|string|string_view)\\b", HighlightToken::type_});
            // 字符串
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"'([^'\\\\]|\\\\.)*'", HighlightToken::string_});
            // 注释
            def.rules.push_back({"//.*$", HighlightToken::comment});
            def.rules.push_back({"/\\*[\\s\\S]*?\\*/", HighlightToken::comment});
            // 数字
            def.rules.push_back({"\\b\\d+(\\.\\d+)?[fFlLuU]*\\b", HighlightToken::number});
            // 函数调用
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()", HighlightToken::function_});
            // 预处理指令
            def.rules.push_back({"^\\s*#\\s*\\w+", HighlightToken::keyword});
            register_language(def);
        }

        // Python
        {
            LanguageDef def;
            def.name = "python";
            def.single_line_comment = "#";
            def.string_delimiters = "\"'";
            def.keywords = {"False","None","True","and","as","assert","async","await","break",
                "class","continue","def","del","elif","else","except","finally","for","from",
                "global","if","import","in","is","lambda","nonlocal","not","or","pass","raise",
                "return","try","while","with","yield"};
            def.rules.push_back({"\\b(False|None|True|and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b(int|float|str|bool|list|dict|tuple|set|bytes|object|type|range|complex)\\b", HighlightToken::type_});
            def.rules.push_back({"\"\"\"[\\s\\S]*?\"\"\"", HighlightToken::string_});
            def.rules.push_back({"'''[\\s\\S]*?'''", HighlightToken::string_});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"'([^'\\\\]|\\\\.)*'", HighlightToken::string_});
            def.rules.push_back({"#.*$", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?[jJ]?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()", HighlightToken::function_});
            def.rules.push_back({"\\b(self|cls)\\b", HighlightToken::type_});
            register_language(def);
        }

        // JavaScript / TypeScript
        {
            LanguageDef def;
            def.name = "javascript";
            def.single_line_comment = "//";
            def.multi_comment_start = "/*";
            def.multi_comment_end = "*/";
            def.string_delimiters = "\"'`";
            def.keywords = {"async","await","break","case","catch","class","const","continue",
                "debugger","default","delete","do","else","export","extends","false","finally",
                "for","function","if","import","in","instanceof","let","new","null","of",
                "return","static","super","switch","this","throw","true","try","typeof",
                "undefined","var","void","while","with","yield"};
            def.rules.push_back({"\\b(async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|export|extends|false|finally|for|function|if|import|in|instanceof|let|new|null|of|return|static|super|switch|this|throw|true|try|typeof|undefined|var|void|while|with|yield)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b(string|number|boolean|object|any|void|never|unknown)\\b", HighlightToken::type_});
            def.rules.push_back({"`[^`]*`", HighlightToken::string_});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"'([^'\\\\]|\\\\.)*'", HighlightToken::string_});
            def.rules.push_back({"//.*$", HighlightToken::comment});
            def.rules.push_back({"/\\*[\\s\\S]*?\\*/", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_$][a-zA-Z0-9_$]*)\\s*(?=\\()", HighlightToken::function_});
            register_language(def);
            // TypeScript 别名
            CompiledLanguage ts_cl = compiled_["javascript"];
            compiled_.emplace("typescript", ts_cl);
            compiled_.emplace("ts", ts_cl);
            compiled_.emplace("js", ts_cl);
        }

        // Shell / Bash
        {
            LanguageDef def;
            def.name = "shell";
            def.single_line_comment = "#";
            def.string_delimiters = "\"'";
            def.keywords = {"if","then","else","elif","fi","for","while","do","done","case",
                "esac","in","function","return","exit","local","export","readonly","unset",
                "shift","source","alias","echo","cd","pwd","ls","grep","find","cat","mkdir","rm"};
            def.rules.push_back({"\\b(if|then|else|elif|fi|for|while|do|done|case|esac|in|function|return|exit|local|export|readonly|unset|shift|source|alias)\\b", HighlightToken::keyword});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"'[^']*'", HighlightToken::string_});
            def.rules.push_back({"#.*$", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+\\b", HighlightToken::number});
            def.rules.push_back({"\\$\\{[^}]+\\}", HighlightToken::type_});
            def.rules.push_back({"\\$[a-zA-Z_][a-zA-Z0-9_]*", HighlightToken::type_});
            register_language(def);
            compiled_.emplace("bash", compiled_["shell"]);
            compiled_.emplace("sh", compiled_["shell"]);
            compiled_.emplace("zsh", compiled_["shell"]);
        }

        // Go
        {
            LanguageDef def;
            def.name = "go";
            def.single_line_comment = "//";
            def.multi_comment_start = "/*";
            def.multi_comment_end = "*/";
            def.string_delimiters = "\"`";
            def.keywords = {"break","case","chan","const","continue","default","defer","else",
                "fallthrough","for","func","go","goto","if","import","interface","map","package",
                "range","return","select","struct","switch","type","var"};
            def.rules.push_back({"\\b(break|case|chan|const|continue|default|defer|else|fallthrough|for|func|go|goto|if|import|interface|map|package|range|return|select|struct|switch|type|var)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b(bool|byte|complex64|complex128|error|float32|float64|int|int8|int16|int32|int64|rune|string|uint|uint8|uint16|uint32|uint64|uintptr)\\b", HighlightToken::type_});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"`[^`]*`", HighlightToken::string_});
            def.rules.push_back({"//.*$", HighlightToken::comment});
            def.rules.push_back({"/\\*[\\s\\S]*?\\*/", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()", HighlightToken::function_});
            register_language(def);
        }

        // Rust
        {
            LanguageDef def;
            def.name = "rust";
            def.single_line_comment = "//";
            def.multi_comment_start = "/*";
            def.multi_comment_end = "*/";
            def.string_delimiters = "\"";
            def.keywords = {"as","async","await","break","const","continue","crate","dyn","else",
                "enum","extern","false","fn","for","if","impl","in","let","loop","match","mod",
                "move","mut","pub","ref","return","self","Self","static","struct","super","trait",
                "true","type","unsafe","use","where","while","yield"};
            def.rules.push_back({"\\b(as|async|await|break|const|continue|crate|dyn|else|enum|extern|false|fn|for|if|impl|in|let|loop|match|mod|move|mut|pub|ref|return|self|Self|static|struct|super|trait|true|type|unsafe|use|where|while|yield)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b(i8|i16|i32|i64|i128|isize|u8|u16|u32|u64|u128|usize|f32|f64|bool|char|str|String|Vec|Box|Option|Result|Rc|Arc)\\b", HighlightToken::type_});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"//.*$", HighlightToken::comment});
            def.rules.push_back({"/\\*[\\s\\S]*?\\*/", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?(_\\d+)*(f32|f64|i8|i16|i32|i64|i128|isize|u8|u16|u32|u64|u128|usize)?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()", HighlightToken::function_});
            register_language(def);
        }

        // SQL
        {
            LanguageDef def;
            def.name = "sql";
            def.single_line_comment = "--";
            def.string_delimiters = "'\"";
            def.keywords = {"SELECT","FROM","WHERE","INSERT","INTO","VALUES","UPDATE","SET",
                "DELETE","CREATE","TABLE","ALTER","DROP","INDEX","JOIN","INNER","LEFT","RIGHT",
                "ON","AND","OR","NOT","NULL","IS","IN","BETWEEN","LIKE","ORDER","BY","GROUP",
                "HAVING","LIMIT","OFFSET","AS","DISTINCT","COUNT","SUM","AVG","MIN","MAX",
                "UNION","ALL","EXISTS","CASE","WHEN","THEN","ELSE","END"};
            def.rules.push_back({"\\b(SELECT|FROM|WHERE|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|TABLE|ALTER|DROP|INDEX|JOIN|INNER|LEFT|RIGHT|ON|AND|OR|NOT|NULL|IS|IN|BETWEEN|LIKE|ORDER|BY|GROUP|HAVING|LIMIT|OFFSET|AS|DISTINCT|COUNT|SUM|AVG|MIN|MAX|UNION|ALL|EXISTS|CASE|WHEN|THEN|ELSE|END)\\b", HighlightToken::keyword});
            def.rules.push_back({"'([^'\\\\]|\\\\.)*'", HighlightToken::string_});
            def.rules.push_back({"--.*$", HighlightToken::comment});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()", HighlightToken::function_});
            register_language(def);
        }

        // JSON
        {
            LanguageDef def;
            def.name = "json";
            def.string_delimiters = "\"";
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"\\s*:", HighlightToken::type_});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"\\b(true|false|null)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b-?\\d+(\\.\\d+)?([eE][+-]?\\d+)?\\b", HighlightToken::number});
            register_language(def);
        }

        // YAML
        {
            LanguageDef def;
            def.name = "yaml";
            def.single_line_comment = "#";
            def.string_delimiters = "\"'";
            def.rules.push_back({"#.*$", HighlightToken::comment});
            def.rules.push_back({"\"([^\"\\\\]|\\\\.)*\"", HighlightToken::string_});
            def.rules.push_back({"'[^']*'", HighlightToken::string_});
            def.rules.push_back({"\\b(true|false|null|yes|no|True|False|None)\\b", HighlightToken::keyword});
            def.rules.push_back({"\\b\\d+(\\.\\d+)?\\b", HighlightToken::number});
            def.rules.push_back({"\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*:", HighlightToken::type_});
            register_language(def);
        }

        // 别名
        compiled_.emplace("c", compiled_["cpp"]);
        compiled_.emplace("h", compiled_["cpp"]);
        compiled_.emplace("hpp", compiled_["cpp"]);
        compiled_.emplace("cc", compiled_["cpp"]);
        compiled_.emplace("cxx", compiled_["cpp"]);
        compiled_.emplace("py", compiled_["python"]);
        compiled_.emplace("golang", compiled_["go"]);
        compiled_.emplace("rs", compiled_["rust"]);
    }
};

}  // namespace ben_gear::cli
