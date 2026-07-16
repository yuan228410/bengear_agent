#pragma once

#include <vector>
#include "base/core/runtime_boundary.hpp"

namespace ben_gear::application {

namespace container = base::container;

enum class CommandRisk {
    read_only,
    workspace_read,
    workspace_write,
    command_execution,
    destructive
};

struct CommandDescriptor {
    std::string action;
    std::string username;
    std::string workspace_name;
    std::string session_id;
    std::string project_path;
    std::string subject;
    CommandRisk risk = CommandRisk::read_only;
    bool mutates_workspace = false;
    bool runs_command = false;
    bool force = false;
    bool staged = false;
    bool worktree = true;
    bool all = false;
    bool amend = false;
    bool create_branch = false;
    int timeout_seconds = 0;
    int max_output_bytes = 0;
    std::string working_directory;
    std::vector<std::string> affected_paths;
};

core::MutationScope command_mutation_scope(CommandRisk risk);
core::RuntimeCapability command_runtime_capability(const CommandDescriptor& command);
core::RuntimeOperation to_runtime_operation(const CommandDescriptor& command);

} // namespace ben_gear::application
