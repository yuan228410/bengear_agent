#pragma once

#include "capabilities/test_loop/types.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace ben_gear::test_loop {

struct DiagnosticParseOptions {
    std::filesystem::path project_root;
    std::filesystem::path cwd;
    int max_diagnostics = 100;
};

struct DiagnosticParseResult {
    std::vector<TestDiagnostic> diagnostics;
    bool truncated = false;
};

DiagnosticParseResult parse_diagnostics(std::string_view output,
                                         const DiagnosticParseOptions& options);

} // namespace ben_gear::test_loop
