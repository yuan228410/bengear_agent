#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

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
    container::String action;
    container::String username;
    container::String workspace_name;
    container::String session_id;
    container::String project_path;
    container::String subject;
    CommandRisk risk = CommandRisk::read_only;
    bool mutates_workspace = false;
    bool runs_command = false;
    bool force = false;
    bool staged = false;
    bool worktree = true;
    bool all = false;
    bool amend = false;
    int timeout_seconds = 0;
    int max_output_bytes = 0;
    container::String working_directory;
    container::Vector<container::String> affected_paths;
};

} // namespace ben_gear::application
