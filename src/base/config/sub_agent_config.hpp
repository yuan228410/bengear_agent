#pragma once

#include <vector>

#include <chrono>
#include <cstdint>

namespace ben_gear::agent {

enum class SessionType : uint8_t {
    main,
    sub_agent,
    workflow
};

struct SubAgentConfig {
    int max_parallel = 5;
    int default_max_steps = 20;
    std::chrono::milliseconds default_timeout{120000};
    bool auto_summary = true;
    int max_output_chars = 4000;
    std::vector<std::string> tool_filter_default;
    std::string model_override;
    int64_t context_length_override = 0;
    bool aggregate_parallel = true;
};

} // namespace ben_gear::agent

namespace ben_gear {
using SubAgentConfig = agent::SubAgentConfig;
using SessionType = agent::SessionType;
}
