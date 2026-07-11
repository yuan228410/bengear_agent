#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"

#include "capabilities/test_loop/diagnostics.hpp"
#include "capabilities/audit/audit_store.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::diagnostic_context {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

domain::AppError app_error(std::string_view type, std::string_view message) {
    auto error = domain::AppError::invalid_argument(
        base::container::String(type.data(), type.size()),
        base::container::String(message.data(), message.size()));
    error.details_json = Json{{"success", false},
                              {"error_type", std::string(type)},
                              {"message", std::string(message)},
                              {"provider", "diagnostic_context"}}
                             .dump();
    return error;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (text.empty()) lines.emplace_back();
    return lines;
}

int clamp_int(int value, int /*fallback*/, int min_value, int max_value) {
    return std::clamp(value, min_value, max_value);
}

std::int64_t clamp_i64(std::int64_t value, std::int64_t /*fallback*/, std::int64_t min_value, std::int64_t max_value) {
    return std::clamp(value, min_value, max_value);
}

bool inside_root(const std::filesystem::path& root, const std::filesystem::path& target) {
    auto root_text = root.string();
    auto target_text = target.string();
    return target_text == root_text || target_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) == 0;
}

std::filesystem::path weak_normal(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) return canonical;
    return path.lexically_normal();
}

bool normalize_relative_path(const std::filesystem::path& root,
                             const std::filesystem::path& cwd,
                             const std::string& input,
                             std::string& normalized) {
    if (input.empty()) return false;
    std::filesystem::path raw(input);
    std::vector<std::filesystem::path> candidates;
    if (raw.is_absolute()) candidates.push_back(raw);
    else {
        candidates.push_back(cwd / raw);
        candidates.push_back(root / raw);
    }
    auto normal_root = weak_normal(root);
    for (const auto& candidate : candidates) {
        auto normal = weak_normal(candidate);
        if (!inside_root(normal_root, normal)) continue;
        auto rel = normal.lexically_relative(normal_root).generic_string();
        if (rel.empty() || rel == "." || rel == ".." || rel.rfind("../", 0) == 0 || rel.find("/../") != std::string::npos) continue;
        normalized = rel;
        return true;
    }
    return false;
}

bool read_file_bounded(const std::filesystem::path& path, std::int64_t max_file_bytes, std::string& content, std::string& error) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > static_cast<std::uintmax_t>(max_file_bytes)) {
        error = "file too large";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "file unavailable";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    if (static_cast<std::int64_t>(content.size()) > max_file_bytes) {
        error = "file too large";
        return false;
    }
    return true;
}

test_loop::TestDiagnostic diagnostic_from_json(const Json& item) {
    test_loop::TestDiagnostic diagnostic;
    if (!item.is_object()) return diagnostic;
    diagnostic.path = item.value("path", "");
    diagnostic.line = item.value("line", 0);
    diagnostic.column = item.value("column", 0);
    diagnostic.end_column = item.value("end_column", 0);
    diagnostic.severity = item.value("severity", "");
    diagnostic.source = item.value("source", "");
    diagnostic.code = item.value("code", "");
    diagnostic.message = item.value("message", "");
    diagnostic.raw = item.value("raw", "");
    diagnostic.test_name = item.value("test_name", "");
    diagnostic.confidence = item.value("confidence", 0);
    return diagnostic;
}

Json diagnostic_json(const test_loop::TestDiagnostic& diagnostic) {
    return Json{{"path", diagnostic.path},
                {"line", diagnostic.line},
                {"column", diagnostic.column},
                {"end_column", diagnostic.end_column},
                {"severity", diagnostic.severity},
                {"source", diagnostic.source},
                {"code", diagnostic.code},
                {"message", diagnostic.message},
                {"raw", diagnostic.raw},
                {"test_name", diagnostic.test_name},
                {"confidence", diagnostic.confidence}};
}

std::string diagnostic_key(const test_loop::TestDiagnostic& diagnostic) {
    return diagnostic.path + "\n" + std::to_string(diagnostic.line) + "\n" + std::to_string(diagnostic.column) + "\n" + diagnostic.message;
}

Json snippet_json(const std::filesystem::path& root,
                  const std::string& path,
                  int line,
                  int context_lines,
                  std::int64_t max_file_bytes,
                  std::int64_t& total_bytes,
                  std::int64_t max_total_bytes,
                  Json& notes) {
    std::string content;
    std::string error;
    if (!read_file_bounded(root / path, max_file_bytes, content, error)) {
        notes.push_back(error);
        return Json();
    }
    auto lines = split_lines(content);
    if (line <= 0 || line > static_cast<int>(lines.size())) {
        notes.push_back("diagnostic line outside file");
        return Json();
    }
    auto start = std::max(1, line - context_lines);
    auto end = std::min(static_cast<int>(lines.size()), line + context_lines);
    Json out_lines = Json::array();
    for (int current = start; current <= end; ++current) {
        const auto& text = lines[static_cast<size_t>(current - 1)];
        total_bytes += static_cast<std::int64_t>(text.size()) + 32;
        if (total_bytes > max_total_bytes) {
            notes.push_back("context byte budget exceeded");
            break;
        }
        Json line_json{{"line", current}, {"text", text}};
        if (current == line) line_json["primary"] = true;
        out_lines.push_back(std::move(line_json));
    }
    return Json{{"path", path}, {"start_line", start}, {"end_line", end}, {"diagnostic_line", line}, {"lines", out_lines}};
}

} // namespace

DiagnosticContextService::DiagnosticContextService(workspace::WorkspaceContext ws_ctx,
                                                   std::shared_ptr<code_intel::CodeIntelService> code_intel_service)
    : ws_ctx_(std::move(ws_ctx)), code_intel_service_(std::move(code_intel_service)) {}

std::filesystem::path DiagnosticContextService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path() : cwd;
}

domain::AppResult<RepairContextRequest> repair_context_request_from_json(const Json& request) {
    if (!request.is_object()) {
        return domain::AppResult<RepairContextRequest>::failure(
            app_error("invalid_arguments", "request must be a JSON object"));
    }

    RepairContextRequest parsed;
    parsed.cwd = request.value("cwd", ".");
    parsed.context_lines = request.value("context_lines", 5);
    parsed.max_diagnostics = request.value("max_diagnostics", 20);
    parsed.max_file_bytes = request.value("max_file_bytes", 1024 * 1024);
    parsed.max_total_bytes = request.value("max_total_bytes", 60000);
    parsed.include_code_intel = request.value("include_code_intel", true);
    parsed.runtime_execution_id = request.value("runtime_execution_id", "");
    if (request.contains("runtime_execution") && request["runtime_execution"].is_object()) {
        parsed.runtime_execution = request["runtime_execution"];
    }
    if (request.contains("code_context") && request["code_context"].is_object()) {
        parsed.code_context = request["code_context"];
    }
    if (request.contains("context_pack") && request["context_pack"].is_object()) {
        parsed.code_context = request["context_pack"];
    }
    if (request.contains("diagnostics") && request["diagnostics"].is_array()) {
        for (const auto& item : request["diagnostics"]) parsed.diagnostics.push_back(diagnostic_from_json(item));
    }
    parsed.output = request.value("output", "");
    return domain::AppResult<RepairContextRequest>::success(std::move(parsed));
}

domain::AppResult<RepairContextResult> DiagnosticContextService::repair_context(RepairContextRequest request) const {
    auto request_session = code_intel_service_ ? code_intel_service_->request_session()
                                               : workspace_index::RequestIndexSession(nullptr);
    return repair_context(std::move(request), request_session);
}

domain::AppResult<RepairContextResult> DiagnosticContextService::repair_context(
    RepairContextRequest request,
    workspace_index::RequestIndexSession& request_session) const {
    if (!request.runtime_execution_id.empty() && (!request.runtime_execution.is_object() || request.runtime_execution.empty())) {
        audit::RuntimeExecutionStore store(ws_ctx_.tier_paths.user_dir / "runtime" / "executions.jsonl");
        auto execution = store.get(base::container::String(request.runtime_execution_id.c_str()));
        if (execution.value("success", false) && execution.contains("execution")) request.runtime_execution = execution["execution"];
    }

    auto root = weak_normal(project_root());
    auto cwd_path = std::filesystem::path(request.cwd);
    std::filesystem::path cwd = cwd_path.is_absolute() ? cwd_path : root / cwd_path;
    cwd = weak_normal(cwd);
    if (!inside_root(root, cwd)) cwd = root;

    auto context_lines = clamp_int(request.context_lines, 5, 0, 50);
    auto max_diagnostics = clamp_int(request.max_diagnostics, 20, 1, 100);
    auto max_file_bytes = clamp_i64(request.max_file_bytes, 1024 * 1024, 1024, 2 * 1024 * 1024);
    auto max_total_bytes = clamp_i64(request.max_total_bytes, 60000, 4096, 200 * 1024);
    auto include_code_intel = request.include_code_intel;

    auto diagnostics = std::move(request.diagnostics);
    bool parse_truncated = false;
    if (diagnostics.empty()) {
        if (request.output.empty()) {
            bool has_runtime_evidence = request.runtime_execution.is_object() && !request.runtime_execution.empty();
            if (!has_runtime_evidence) {
                return domain::AppResult<RepairContextResult>::failure(
                    app_error("invalid_arguments", "diagnostics, output, or runtime_execution is required"));
            }
        }
        if (!request.output.empty()) {
        auto parsed = test_loop::parse_diagnostics(request.output, test_loop::DiagnosticParseOptions{root, cwd, max_diagnostics});
        diagnostics = std::move(parsed.diagnostics);
        parse_truncated = parsed.truncated;
        }
    }

    Json contexts = Json::array();
    Json files = Json::array();
    std::set<std::string> seen;
    std::map<std::string, int> file_counts;
    bool truncated = parse_truncated;
    std::int64_t total_bytes = 0;
    int processed = 0;

    for (auto diagnostic : diagnostics) {
        auto key = diagnostic_key(diagnostic);
        if (!seen.insert(key).second) continue;
        if (processed >= max_diagnostics) {
            truncated = true;
            break;
        }
        ++processed;

        Json notes = Json::array();
        std::string normalized;
        if (!diagnostic.path.empty()) {
            if (normalize_relative_path(root, cwd, diagnostic.path, normalized)) diagnostic.path = normalized;
            else {
                notes.push_back("diagnostic path outside workspace");
                diagnostic.path.clear();
            }
        }

        Json item{{"diagnostic", diagnostic_json(diagnostic)}, {"symbols", Json::array()}, {"definitions", Json::array()}};
        if (!diagnostic.path.empty()) {
            file_counts[diagnostic.path] += 1;
            auto snippet = snippet_json(root, diagnostic.path, diagnostic.line, context_lines, max_file_bytes, total_bytes, max_total_bytes, notes);
            if (!snippet.is_null()) item["snippet"] = std::move(snippet);

            if (include_code_intel && code_intel_service_) {
                auto symbols = code_intel_service_->document_symbols(diagnostic.path, request_session);
                if (symbols.ok()) item["symbols"] = code_intel::to_json(symbols.value())["symbols"];
                if (diagnostic.line > 0 && diagnostic.column > 0) {
                    code_intel::CodeIntelQuery query;
                    query.path = diagnostic.path;
                    query.line = diagnostic.line;
                    query.column = diagnostic.column;
                    query.limit = 5;
                    auto definitions = code_intel_service_->definition(query, code_intel::CodeIntelOptions{}, request_session);
                    if (definitions.ok()) item["definitions"] = code_intel::to_json(definitions.value())["definitions"];
                }
            }
        }
        item["notes"] = std::move(notes);
        contexts.push_back(std::move(item));
    }

    for (const auto& [path, count] : file_counts) files.push_back(Json{{"path", path}, {"diagnostic_count", count}});

    RepairContextResult result;
    result.diagnostic_count = static_cast<int>(contexts.size());
    result.truncated = truncated;
    result.contexts = std::move(contexts);
    result.files = files;
    result.runtime_execution = std::move(request.runtime_execution);
    result.code_context = request.code_context.is_object() && !request.code_context.empty()
        ? request.code_context
        : Json{{"primary_files", files},
               {"contexts", contexts},
               {"impact_summary", Json{{"primary_file_count", static_cast<int>(files.size())}, {"context_count", static_cast<int>(contexts.size())}}}};
    return domain::AppResult<RepairContextResult>::success(std::move(result));
}

Json to_json(const RepairContextResult& result) {
    return Json{{"success", true},
                {"provider", "diagnostic_context"},
                {"diagnostic_count", result.diagnostic_count},
                {"truncated", result.truncated},
                {"contexts", result.contexts},
                {"files", result.files},
                {"runtime_execution", result.runtime_execution},
                {"code_context", result.code_context}};
}

} // namespace ben_gear::diagnostic_context
