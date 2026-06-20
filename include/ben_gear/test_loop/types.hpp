#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::test_loop {

struct TestCommandSuggestion {
    std::string id;
    std::string command;
    std::string cwd;
    std::string reason;
    int confidence = 0;
};

struct TestDiagnostic {
    std::string path;
    int line = 0;
    int column = 0;
    int end_column = 0;
    std::string severity;
    std::string source;
    std::string code;
    std::string message;
    std::string raw;
    std::string test_name;
    int confidence = 0;
};

struct TestRunResult {
    bool success = false;
    bool timed_out = false;
    int exit_code = -1;
    int elapsed_ms = 0;
    std::string command;
    std::string cwd;
    std::string output;
    std::vector<std::string> failure_summary;
    std::vector<TestDiagnostic> diagnostics;
    bool diagnostics_truncated = false;
};

Json to_json(const TestCommandSuggestion& suggestion);
Json to_json(const TestDiagnostic& diagnostic);
Json to_json(const TestRunResult& result);

} // namespace ben_gear::test_loop
