#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <chrono>
#include <cstdint>

namespace ben_gear::agent {

namespace container = base::container;

// ==================== 会话类型 ====================

enum class SessionType : uint8_t {
    main,       // 主 Agent
    sub_agent,  // 子 Agent
    workflow    // 工作流
};

// ==================== 子 Agent 配置 ====================

struct SubAgentConfig {
    int max_parallel = 5;
    int default_max_steps = 20;
    std::chrono::milliseconds default_timeout{120000};
    bool auto_summary = true;
    int max_output_chars = 4000;
    container::Vector<container::String> tool_filter_default;
    container::String model_override;
    int64_t context_length_override = 0;
    bool aggregate_parallel = true;
};

} // namespace ben_gear::agent

namespace ben_gear {
using SubAgentConfig = agent::SubAgentConfig;
using SessionType = agent::SessionType;
}
