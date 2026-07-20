#pragma once

#include "config/settings.hpp"
#include "workspace/types.hpp"

#include <string>

namespace ben_gear::cli {

ben_gear::workspace::WorkspaceContext build_ws_ctx(const Config& config);

struct SessionRunnerOptions {
    bool markdown_raw = false;
    bool show_banner = true;
    bool hide_thinking = false;
    bool hide_tool = false;
    bool hide_detail = false;
};

int run_chat_session(const Config& config,
                     const SessionRunnerOptions& options,
                     bool force_new_session);

int run_single_request_session(const Config& config,
                               std::string prompt,
                               const SessionRunnerOptions& options,
                               bool async_mode);

}  // namespace ben_gear::cli
