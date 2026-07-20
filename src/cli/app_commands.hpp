#pragma once

#include "cli/args.hpp"
#include "config/settings.hpp"

#include <string>
#include <vector>

namespace ben_gear::cli {

std::string join_prompt(const std::vector<std::string>& parts);

void print_config(const Config& config);

int run_workspace_command(const Config& config, const Parsed& parsed);
int run_session_command(const Config& config, const Parsed& parsed);
int run_serve_command(const Config& config);
int run_list_skills_command(const Config& config);

}  // namespace ben_gear::cli
