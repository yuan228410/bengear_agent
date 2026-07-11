#include "capabilities/test_loop/diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>

#include "capabilities/test_loop/internal/parse_util.hpp"

namespace ben_gear::test_loop {

namespace {

std::string normalize_severity(std::string value) {
    value = lower_copy(trim(std::move(value)));
    if (value == "fatal error") return "error";
    if (value == "failure") return "failure";
    if (value == "assertionerror") return "failure";
    if (value == "error" || value == "warning" || value == "note" || value == "info") return value;
    return "unknown";
}

std::string strip_quotes(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string slashify(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::filesystem::path lexical_abs(const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.lexically_normal();
}

bool inside_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto root_text = root.string();
    auto candidate_text = candidate.string();
    return candidate_text == root_text || candidate_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) == 0;
}

bool normalize_path(const std::string& raw_path, const DiagnosticParseOptions& options, std::string& normalized) {
    auto text = slashify(strip_quotes(raw_path));
    if (text.empty()) return false;
    auto root = lexical_abs(options.project_root);
    auto cwd = options.cwd.empty() ? root : lexical_abs(options.cwd);
    auto try_candidate = [&](const std::filesystem::path& candidate) -> bool {
        auto normal = lexical_abs(candidate);
        if (!inside_root(root, normal)) return false;
        auto rel = normal.lexically_relative(root).generic_string();
        if (rel.empty() || rel == ".") return false;
        normalized = rel;
        return true;
    };

    std::filesystem::path path(text);
    if (path.is_absolute()) return try_candidate(path);
    if (try_candidate(cwd / path)) return true;
    return try_candidate(root / path);
}

int to_int(const std::string& value) {
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

bool add_diagnostic(DiagnosticParseResult& result,
                    const DiagnosticParseOptions& options,
                    TestDiagnostic diagnostic,
                    int limit) {
    if (static_cast<int>(result.diagnostics.size()) >= limit) {
        result.truncated = true;
        return false;
    }
    if (!diagnostic.path.empty()) {
        std::string normalized;
        if (!normalize_path(diagnostic.path, options, normalized)) return true;
        diagnostic.path = std::move(normalized);
    }
    diagnostic.raw = trim(std::move(diagnostic.raw));
    diagnostic.message = trim(std::move(diagnostic.message));
    result.diagnostics.push_back(std::move(diagnostic));
    return true;
}

bool parse_msvc_line(const std::string& line, const DiagnosticParseOptions& options, DiagnosticParseResult& result, int limit) {
    static const std::regex pattern(R"(^(.+)\((\d+)(?:,(\d+))?\):\s*(error|warning)\s*([A-Za-z]+\d+)?:?\s*(.*)$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) return false;
    TestDiagnostic diagnostic;
    diagnostic.path = match[1].str();
    diagnostic.line = to_int(match[2].str());
    diagnostic.column = match[3].matched ? to_int(match[3].str()) : 0;
    diagnostic.severity = normalize_severity(match[4].str());
    diagnostic.source = "msvc";
    diagnostic.code = match[5].matched ? match[5].str() : std::string();
    diagnostic.message = match[6].str();
    diagnostic.raw = line;
    diagnostic.confidence = 95;
    add_diagnostic(result, options, std::move(diagnostic), limit);
    return true;
}

bool parse_gcc_line(const std::string& line, const DiagnosticParseOptions& options, DiagnosticParseResult& result, int limit) {
    static const std::regex pattern(R"(^(.+?):(\d+)(?::(\d+))?:\s*(fatal error|error|warning|note|Failure|AssertionError):?\s*(.*)$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) return false;
    auto severity = normalize_severity(match[4].str());
    TestDiagnostic diagnostic;
    diagnostic.path = match[1].str();
    diagnostic.line = to_int(match[2].str());
    diagnostic.column = match[3].matched ? to_int(match[3].str()) : 0;
    diagnostic.severity = severity;
    diagnostic.source = severity == "failure" ? "gtest" : "gcc";
    diagnostic.message = match[5].str();
    diagnostic.raw = line;
    diagnostic.confidence = severity == "failure" ? 90 : 95;
    add_diagnostic(result, options, std::move(diagnostic), limit);
    return true;
}

bool parse_pytest_file_line(const std::string& line, const DiagnosticParseOptions& options, DiagnosticParseResult& result, int limit) {
    static const std::regex pattern(R"PY(^\s*File\s+"([^"]+)",\s+line\s+(\d+)(?:,\s+in\s+(.+))?.*$)PY");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) return false;
    TestDiagnostic diagnostic;
    diagnostic.path = match[1].str();
    diagnostic.line = to_int(match[2].str());
    diagnostic.severity = "failure";
    diagnostic.source = "pytest";
    diagnostic.message = match[3].matched ? match[3].str() : "python traceback";
    diagnostic.test_name = match[3].matched ? trim(match[3].str()) : std::string();
    diagnostic.raw = line;
    diagnostic.confidence = 85;
    add_diagnostic(result, options, std::move(diagnostic), limit);
    return true;
}

bool parse_failed_test_line(const std::string& line, DiagnosticParseResult& result, int limit) {
    static const std::regex pattern(R"(^\[\s*FAILED\s*\]\s+(.+)$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) return false;
    if (static_cast<int>(result.diagnostics.size()) >= limit) {
        result.truncated = true;
        return true;
    }
    TestDiagnostic diagnostic;
    diagnostic.severity = "failure";
    diagnostic.source = "gtest";
    diagnostic.test_name = trim(match[1].str());
    diagnostic.message = "test failed";
    diagnostic.raw = line;
    diagnostic.confidence = 70;
    result.diagnostics.push_back(std::move(diagnostic));
    return true;
}

bool parse_generic_line(const std::string& line, DiagnosticParseResult& result, int limit) {
    if (!looks_like_failure_line(line)) return false;
    if (static_cast<int>(result.diagnostics.size()) >= limit) {
        result.truncated = true;
        return true;
    }
    TestDiagnostic diagnostic;
    diagnostic.severity = lower_copy(line).find("warning") != std::string::npos ? "warning" : "failure";
    diagnostic.source = "generic";
    diagnostic.message = line;
    diagnostic.raw = line;
    diagnostic.confidence = 40;
    result.diagnostics.push_back(std::move(diagnostic));
    return true;
}

} // namespace

DiagnosticParseResult parse_diagnostics(std::string_view output,
                                         const DiagnosticParseOptions& options) {
    DiagnosticParseResult result;
    auto limit = std::clamp(options.max_diagnostics > 0 ? options.max_diagnostics : 100, 1, 500);
    for (auto line : split_lines(output)) {
        line = trim(std::move(line));
        if (line.empty()) continue;
        if (parse_msvc_line(line, options, result, limit)) continue;
        if (parse_gcc_line(line, options, result, limit)) continue;
        if (parse_pytest_file_line(line, options, result, limit)) continue;
        if (parse_failed_test_line(line, result, limit)) continue;
        parse_generic_line(line, result, limit);
        if (result.truncated) break;
    }
    return result;
}

} // namespace ben_gear::test_loop
