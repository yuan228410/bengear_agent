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

struct TestRunResult {
    bool success = false;
    bool timed_out = false;
    int exit_code = -1;
    int elapsed_ms = 0;
    std::string command;
    std::string cwd;
    std::string output;
    std::vector<std::string> failure_summary;
};

Json to_json(const TestCommandSuggestion& suggestion);
Json to_json(const TestRunResult& result);

} // namespace ben_gear::test_loop
