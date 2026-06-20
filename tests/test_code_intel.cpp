#include "ben_gear/code_intel/code_intel_service.hpp"
#include "ben_gear/tools/code_intel_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

using bengear::test::TmpDirTest;

class CodeIntelServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("code-intel-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

void create_code_intel_project(const std::filesystem::path& root) {
    write_text(root / "include/foo.hpp",
               "#pragma once\n"
               "namespace demo {\n"
               "class Foo {\n"
               "public:\n"
               "  void run();\n"
               "};\n"
               "}\n");
    write_text(root / "src/foo.cpp",
               "#include \"foo.hpp\"\n"
               "namespace demo {\n"
               "void use() {\n"
               "  Foo value;\n"
               "  int Food = 0;\n"
               "  value.run();\n"
               "}\n"
               "}\n");
    write_text(root / "tests/test_foo.cpp", "#include \"foo.hpp\"\nint main() { demo::Foo value; return 0; }\n");
}

bool has_location(const ben_gear::Json& array, const std::string& path, const std::string& symbol) {
    if (!array.is_array()) return false;
    for (const auto& item : array) {
        if (item.value("path", "") == path && item.value("symbol", "") == symbol) return true;
    }
    return false;
}

} // namespace

TEST_F(CodeIntelServiceTest, CapabilitiesReportsIndexedProvider) {
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    auto result = service.capabilities();
    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("provider", ""), "indexed");
    EXPECT_FALSE(result.value("real_lsp", true));
    EXPECT_TRUE(result["capabilities"].value("document_symbols", false));
    EXPECT_TRUE(result["capabilities"].value("definition", false));
    EXPECT_TRUE(result["capabilities"].value("references", false));
}

TEST_F(CodeIntelServiceTest, DocumentSymbolsReturnsSymbolsForPath) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    auto result = service.document_symbols("include/foo.hpp");
    ASSERT_TRUE(result.value("success", false));
    EXPECT_TRUE(has_location(result["symbols"], "include/foo.hpp", "Foo"));
}

TEST_F(CodeIntelServiceTest, DefinitionFindsBySymbol) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    ben_gear::code_intel::CodeIntelQuery query;
    query.symbol = "Foo";
    auto result = service.definition(query);
    ASSERT_TRUE(result.value("success", false));
    EXPECT_TRUE(has_location(result["definitions"], "include/foo.hpp", "Foo"));
}

TEST_F(CodeIntelServiceTest, DefinitionExtractsTokenFromPosition) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    ben_gear::code_intel::CodeIntelQuery query;
    query.path = "src/foo.cpp";
    query.line = 4;
    query.column = 4;
    auto result = service.definition(query);
    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("symbol", ""), "Foo");
    EXPECT_TRUE(has_location(result["definitions"], "include/foo.hpp", "Foo"));
}

TEST_F(CodeIntelServiceTest, ReferencesFindsWholeWordOccurrences) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    ben_gear::code_intel::CodeIntelQuery query;
    query.symbol = "Foo";
    query.limit = 20;
    auto result = service.references(query);
    ASSERT_TRUE(result.value("success", false));
    EXPECT_TRUE(has_location(result["references"], "include/foo.hpp", "Foo"));
    EXPECT_TRUE(has_location(result["references"], "src/foo.cpp", "Foo"));
    for (const auto& ref : result["references"]) {
        EXPECT_THAT(ref.value("preview", ""), testing::Not(testing::HasSubstr("Food")));
    }
}

TEST_F(CodeIntelServiceTest, RejectsWorkspaceEscape) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    auto result = service.document_symbols("../outside.cpp");
    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("error_type", ""), "path_outside_workspace");
}

TEST_F(CodeIntelServiceTest, ClampsLimitAndMarksTruncated) {
    create_code_intel_project(dir());
    auto service = ben_gear::code_intel::CodeIntelService(make_ctx(dir()));
    ben_gear::code_intel::CodeIntelQuery query;
    query.symbol = "Foo";
    query.limit = 1;
    auto result = service.references(query);
    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["references"].size(), 1u);
    EXPECT_TRUE(result.value("truncated", false));
}

TEST_F(CodeIntelServiceTest, ToolRegistrationMarksCodeIntelToolsReadOnly) {
    auto service = std::make_shared<ben_gear::code_intel::CodeIntelService>(make_ctx(dir()));
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_code_intel_tools(registry, service);

    EXPECT_TRUE(registry.is_read_only("code_intel_document_symbols"));
    EXPECT_TRUE(registry.is_read_only("code_intel_definition"));
    EXPECT_TRUE(registry.is_read_only("code_intel_references"));
}
