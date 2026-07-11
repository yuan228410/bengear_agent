#pragma once

#include "cli/render/display_config.hpp"
#include "agent/event_sink.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::cli {

class Renderer;

/// Builds the adapter that translates agent/runtime callbacks into renderer calls.
///
/// This keeps agent DTO formatting/truncation at the CLI adapter boundary so
/// CliApp remains a small composition object and Renderer stays focused on UI.
std::unique_ptr<agent::AgentEventSink> make_agent_event_sink_adapter(
    Renderer& renderer,
    DisplayConfig config,
    std::string_view model_name,
    int64_t context_length);

}  // namespace ben_gear::cli
