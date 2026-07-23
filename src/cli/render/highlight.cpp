#include "cli/render/highlight.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace ben_gear::cli {

// ==================== SyntaxHighlighter ====================

SyntaxHighlighter::SyntaxHighlighter(const Theme& theme, const TerminalCapabilities& cap)
    : theme_(theme), cap_(cap) {
    register_builtin_languages();
}

std::string SyntaxHighlighter::highlight(std::string_view code, std::string_view lang) const {
    auto it = compiled_.find(std::string(lang));
    if (it == compiled_.end()) {
        return std::string(code);
    }
    return highlight_line(code, it->second);
}

bool SyntaxHighlighter::supports(std::string_view lang) const {
    return compiled_.find(std::string(lang)) != compiled_.end();
}

void SyntaxHighlighter::register_language(const LanguageDef& def) {
    CompiledLanguage cl;
    cl.name = def.name;
    cl.keywords = def.keywords;
    cl.types = def.types;
    cl.single_line_comment = def.single_line_comment;
    cl.multi_comment_start = def.multi_comment_start;
    cl.multi_comment_end = def.multi_comment_end;
    cl.string_delimiters = def.string_delimiters;
    // 排序以支持二分查找（可选优化，当前线性查找已足够）
    std::sort(cl.keywords.begin(), cl.keywords.end());
    std::sort(cl.types.begin(), cl.types.end());
    compiled_.emplace(def.name, std::move(cl));
}

const Color& SyntaxHighlighter::token_color(HighlightToken token) const {
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

void SyntaxHighlighter::append_colored(std::string& out, std::string_view text, const Color& color) const {
    auto color_code = ansi::fg(color, cap_);
    auto& reset_code = ansi::reset();
    if (!color_code.empty()) out.append(color_code.data(), color_code.size());
    out.append(text.data(), text.size());
    if (!reset_code.empty()) out.append(reset_code.data(), reset_code.size());
}

// ==================== 手写扫描器（零正则，O(n) 单遍） ====================

std::string SyntaxHighlighter::highlight_line(std::string_view line, const CompiledLanguage& lang) const {
    if (!cap_.color || line.empty()) {
        return std::string(line);
    }

    std::string result;
    result.reserve(line.size() + 64);

    const size_t n = line.size();
    size_t i = 0;

    // 跨行多行注释延续
    if (in_multi_comment_) {
        size_t end = line.find(lang.multi_comment_end);
        if (end != std::string_view::npos) {
            append_colored(result, line.substr(0, end + lang.multi_comment_end.size()),
                           token_color(HighlightToken::comment));
            in_multi_comment_ = false;
            i = end + lang.multi_comment_end.size();
        } else {
            append_colored(result, line, token_color(HighlightToken::comment));
            return result;
        }
    }

    while (i < n) {
        char c = line[i];

        // ---- 空白 ----
        if (c == ' ' || c == '\t') {
            result.push_back(c);
            ++i;
            continue;
        }

        // ---- 单行注释 ----
        if (!lang.single_line_comment.empty()) {
            auto& sc = lang.single_line_comment;
            if (i + sc.size() <= n && line.compare(i, sc.size(), sc) == 0) {
                append_colored(result, line.substr(i), token_color(HighlightToken::comment));
                return result;
            }
        }

        // ---- 多行注释开始 ----
        if (!lang.multi_comment_start.empty()) {
            auto& ms = lang.multi_comment_start;
            if (i + ms.size() <= n && line.compare(i, ms.size(), ms) == 0) {
                size_t end = line.find(lang.multi_comment_end, i + ms.size());
                if (end != std::string_view::npos) {
                    size_t len = end - i + lang.multi_comment_end.size();
                    append_colored(result, line.substr(i, len), token_color(HighlightToken::comment));
                    i += len;
                    continue;
                } else {
                    in_multi_comment_ = true;
                    append_colored(result, line.substr(i), token_color(HighlightToken::comment));
                    return result;
                }
            }
        }

        // ---- 字符串（支持转义） ----
        if (!lang.string_delimiters.empty()) {
            // 检测三引号字符串（Python """ 或 '''）
            if (i + 2 < n && (c == '"' || c == '\'') && line[i+1] == c && line[i+2] == c) {
                char delim = c;
                size_t j = i + 3;
                while (j + 2 < n) {
                    if (line[j] == delim && line[j+1] == delim && line[j+2] == delim) {
                        j += 3;
                        break;
                    }
                    ++j;
                }
                append_colored(result, line.substr(i, j - i), token_color(HighlightToken::string_));
                i = j;
                continue;
            }

            // 检测单引号字符串
            for (char delim : lang.string_delimiters) {
                if (c == delim) {
                    size_t j = i + 1;
                    while (j < n) {
                        if (line[j] == '\\' && j + 1 < n) { j += 2; continue; }
                        if (line[j] == delim) { ++j; break; }
                        ++j;
                    }
                    append_colored(result, line.substr(i, j - i), token_color(HighlightToken::string_));
                    i = j;
                    goto continue_outer;
                }
            }
        }

        // ---- 预处理指令（C/C++ #directive） ----
        if (c == '#' && (lang.name == "cpp" || lang.name == "c" || lang.name == "h" || lang.name == "hpp")) {
            size_t j = i + 1;
            // 跳过前导空白
            while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
            // 扫描到 word 边界
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            append_colored(result, line.substr(i, j - i), token_color(HighlightToken::keyword));
            i = j;
            continue;
        }

        // ---- Shell 变量 $VAR / ${VAR} ----
        if (c == '$' && (lang.name == "shell" || lang.name == "bash" || lang.name == "sh" || lang.name == "zsh")) {
            size_t j = i + 1;
            if (j < n && line[j] == '{') {
                while (j < n && line[j] != '}') ++j;
                if (j < n) ++j;  // 包含 }
            } else {
                while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            }
            append_colored(result, line.substr(i, j - i), token_color(HighlightToken::type_));
            i = j;
            continue;
        }

        // ---- 数字 ----
        if (c >= '0' && c <= '9') {
            size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.' || line[j] == '_')) ++j;
            append_colored(result, line.substr(i, j - i), token_color(HighlightToken::number));
            i = j;
            continue;
        }

        // ---- 标识符（关键字 / 类型 / 函数调用） ----
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            std::string_view word(line.data() + i, j - i);

            // 检查是否为函数调用（后面紧跟 '('，可选空白）
            size_t k = j;
            while (k < n && (line[k] == ' ' || line[k] == '\t')) ++k;
            bool is_func_call = (k < n && line[k] == '(');

            // 查关键字表（已排序，二分查找）
            bool found = std::binary_search(lang.keywords.begin(), lang.keywords.end(), std::string(word));
            if (found) {
                append_colored(result, word, token_color(HighlightToken::keyword));
                i = j;
                continue;
            }

            // 查类型表
            if (!lang.types.empty()) {
                found = std::binary_search(lang.types.begin(), lang.types.end(), std::string(word));
                if (found) {
                    append_colored(result, word, token_color(HighlightToken::type_));
                    i = j;
                    continue;
                }
            }

            // 函数调用（不在关键字/类型表中，且后面跟 '('）
            if (is_func_call) {
                append_colored(result, word, token_color(HighlightToken::function_));
                i = j;
                continue;
            }

            // JSON/YAML key: 模式（"word": 或 word:）
            if ((lang.name == "json" || lang.name == "yaml") && k < n && line[k] == ':') {
                append_colored(result, word, token_color(HighlightToken::type_));
                i = j;
                continue;
            }

            // 普通标识符
            result.append(word.data(), word.size());
            i = j;
            continue;
        }

        // ---- 默认：原样输出 ----
        result.push_back(c);
        ++i;

    continue_outer:;
    }

    return result;
}

// ==================== 内置语言注册 ====================

void SyntaxHighlighter::register_builtin_languages() {
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
        def.types = {"int","long","short","float","double","char","void","bool","unsigned","signed",
            "size_t","uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t",
            "int64_t","string","string_view","auto"};
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
        def.types = {"int","float","str","bool","list","dict","tuple","set","bytes","object",
            "type","range","complex","self","cls"};
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
        def.types = {"string","number","boolean","object","any","void","never","unknown"};
        register_language(def);
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
        register_language(def);
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
        def.types = {"bool","byte","complex64","complex128","error","float32","float64","int",
            "int8","int16","int32","int64","rune","string","uint","uint8","uint16","uint32",
            "uint64","uintptr"};
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
        def.types = {"i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128",
            "usize","f32","f64","bool","char","str","String","Vec","Box","Option","Result",
            "Rc","Arc"};
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
        register_language(def);
    }

    // JSON
    {
        LanguageDef def;
        def.name = "json";
        def.string_delimiters = "\"";
        def.keywords = {"true","false","null"};
        register_language(def);
    }

    // YAML
    {
        LanguageDef def;
        def.name = "yaml";
        def.single_line_comment = "#";
        def.string_delimiters = "\"'";
        def.keywords = {"true","false","null","yes","no","True","False","None"};
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
    compiled_.emplace("bash", compiled_["shell"]);
    compiled_.emplace("sh", compiled_["shell"]);
    compiled_.emplace("zsh", compiled_["shell"]);
    compiled_.emplace("typescript", compiled_["javascript"]);
    compiled_.emplace("ts", compiled_["javascript"]);
    compiled_.emplace("js", compiled_["javascript"]);
}

}  // namespace ben_gear::cli
