#include "ben_gear/diagnostic_context/diagnostic_context_service.hpp"
#include "ben_gear/tools/diagnostic_context_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

using bengear::test::TmpDirTest;

class DiagnosticContextServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("diagnostic-context-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

ben_gear::Json diagnostic(std::string_view path, int line, int column, std::string_view message = "bad thing") {
    return ben_gear::Json{{"path", std::string(path)},
                          {"line", line},
                          {"column", column},
                          {"severity", "error"},
                          {"source", "gcc"},
                          {"message", std::string(message)},
                          {"confidence", 90}};
}

ben_gear::Json diagnostic_context_result_json(
    const ben_gear::domain::AppResult<ben_gear::diagnostic_context::RepairContextResult>& result) {
    return result.ok() ? ben_gear::diagnostic_context::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", std::string(result.error().code.c_str())},
                                        {"message", std::string(result.error().message.c_str())},
                                        {"provider", "diagnostic_context"}};
}

ben_gear::domain::AppResult<ben_gear::diagnostic_context::RepairContextResult> repair_context(
    ben_gear::diagnostic_context::DiagnosticContextService& service,
    const ben_gear::Json& request) {
    auto parsed = ben_gear::diagnostic_context::repair_context_request_from_json(request);
    if (!parsed.ok()) {
        return ben_gear::domain::AppResult<ben_gear::diagnostic_context::RepairContextResult>::failure(parsed.error());
    }
    return service.repair_context(std::move(parsed.value()));
}

} // namespace

TEST_F(DiagnosticContextServiceTest, BuildsSnippetForDiagnostic) {
    write_text(dir() / "src/foo.cpp", "int one = 1;\nint two = 2;\nint three = nope;\nint four = 4;\n");
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 3, 13)})},
                                                        {"context_lines", 1}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["contexts"].size(), 1u);
    const auto& item = result["contexts"][0];
    EXPECT_EQ(item["diagnostic"].value("path", ""), "src/foo.cpp");
    ASSERT_TRUE(item.contains("snippet"));
    EXPECT_EQ(item["snippet"].value("path", ""), "src/foo.cpp");
    EXPECT_EQ(item["snippet"].value("start_line", 0), 2);
    EXPECT_EQ(item["snippet"].value("end_line", 0), 4);
    ASSERT_EQ(item["snippet"]["lines"].size(), 3u);
    EXPECT_TRUE(item["snippet"]["lines"][1].value("primary", false));
    EXPECT_THAT(item["snippet"]["lines"][1].value("text", ""), testing::HasSubstr("nope"));
}

TEST_F(DiagnosticContextServiceTest, ClampsSnippetAtFileBounds) {
    write_text(dir() / "src/foo.cpp", "first\nsecond\nthird\n");
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 1)})},
                                                        {"context_lines", 5}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["contexts"][0]["snippet"].value("start_line", 0), 1);
    EXPECT_EQ(result["contexts"][0]["snippet"].value("diagnostic_line", 0), 1);
}

TEST_F(DiagnosticContextServiceTest, ParsesRawOutputFallback) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return missing;\n}\n");
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"output", "src/foo.cpp:2:10: error: missing value\n"},
                                                        {"context_lines", 0}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["contexts"].size(), 1u);
    EXPECT_EQ(result["contexts"][0]["diagnostic"].value("path", ""), "src/foo.cpp");
    EXPECT_EQ(result["contexts"][0]["diagnostic"].value("line", 0), 2);
}

TEST_F(DiagnosticContextServiceTest, DeduplicatesDiagnostics) {
    write_text(dir() / "src/foo.cpp", "int main() { return nope; }\n");
    auto diag = diagnostic("src/foo.cpp", 1, 21);
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diag, diag})}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["contexts"].size(), 1u);
}

TEST_F(DiagnosticContextServiceTest, SkipsWorkspaceEscapePathContent) {
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("../outside.cpp", 1, 1)})}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["contexts"].size(), 1u);
    EXPECT_TRUE(result["contexts"][0]["diagnostic"].value("path", "").empty());
    EXPECT_FALSE(result["contexts"][0].contains("snippet"));
    ASSERT_GE(result["contexts"][0]["notes"].size(), 1u);
}

TEST_F(DiagnosticContextServiceTest, HonorsMaxDiagnosticsAndMarksTruncated) {
    write_text(dir() / "src/foo.cpp", "one\ntwo\nthree\n");
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 1, "one"),
                                                                                              diagnostic("src/foo.cpp", 2, 1, "two")})},
                                                        {"max_diagnostics", 1}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["contexts"].size(), 1u);
    EXPECT_TRUE(result.value("truncated", false));
}

TEST_F(DiagnosticContextServiceTest, HonorsMaxFileBytes) {
    write_text(dir() / "src/large.cpp", std::string(2048, 'x'));
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()));

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/large.cpp", 1, 1)})},
                                                        {"max_file_bytes", 1}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_FALSE(result["contexts"][0].contains("snippet"));
    ASSERT_GE(result["contexts"][0]["notes"].size(), 1u);
    EXPECT_THAT(result["contexts"][0]["notes"][0].get<std::string>(), testing::HasSubstr("file too large"));
}

TEST_F(DiagnosticContextServiceTest, IncludesCodeIntelEnrichmentBestEffort) {
    write_text(dir() / "src/foo.cpp", "class Foo {};\nint main() {\n  Foo value;\n}\n");
    auto repo_service = std::make_shared<ben_gear::repo_map::RepoMapService>(make_ctx(dir()));
    auto code_service = std::make_shared<ben_gear::code_intel::CodeIntelService>(make_ctx(dir()), repo_service);
    ben_gear::diagnostic_context::DiagnosticContextService service(make_ctx(dir()), code_service);

    auto result = diagnostic_context_result_json(repair_context(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 3, 4)})},
                                                        {"include_code_intel", true}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["contexts"].size(), 1u);
    EXPECT_TRUE(result["contexts"][0].contains("symbols"));
    EXPECT_TRUE(result["contexts"][0].contains("definitions"));
}

TEST_F(DiagnosticContextServiceTest, ToolRegistrationMarksDiagnosticContextReadOnly) {
    auto service = std::make_shared<ben_gear::diagnostic_context::DiagnosticContextService>(make_ctx(dir()));
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_diagnostic_context_tools(registry, service);

    EXPECT_TRUE(registry.is_read_only("diagnostic_repair_context"));
}
