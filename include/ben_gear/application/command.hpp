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
    CommandRisk risk = CommandRisk::read_only;
    bool mutates_workspace = false;
    bool runs_command = false;
    container::Vector<container::String> affected_paths;
};

} // namespace ben_gear::application
