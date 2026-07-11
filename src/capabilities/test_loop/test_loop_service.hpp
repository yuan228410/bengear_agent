#pragma once

#include "base/domain/result.hpp"
#include "capabilities/test_loop/types.hpp"
#include "workspace/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ben_gear::test_loop {

class TestLoopService {
public:
    explicit TestLoopService(workspace::WorkspaceContext ws_ctx);

    domain::AppResult<TestLoopInspectResult> inspect() const;
    domain::AppResult<TestRunResult> run(const std::string& command,
                                         const std::string& cwd = {},
                                         int timeout_seconds = 120,
                                         int max_output_bytes = 60000) const;

private:
    struct CommandResult {
        int exit_code = -1;
        bool timed_out = false;
        int elapsed_ms = 0;
        std::string output;
    };

    std::filesystem::path project_root() const;
    bool validate_cwd(const std::string& input, std::filesystem::path& resolved, std::string& error) const;
    std::vector<TestCommandSuggestion> detect_commands() const;
    std::vector<std::string> parse_failures(const std::string& output, int max_items = 20) const;
    CommandResult run_command(const std::string& command, const std::filesystem::path& cwd, int timeout_seconds, int max_output_bytes) const;

    workspace::WorkspaceContext ws_ctx_;
};

} // namespace ben_gear::test_loop
