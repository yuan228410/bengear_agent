#pragma once

#include "agent/core/interfaces.hpp"
#include "agent/runtime/runtime.hpp"
#include "acp/types/tool_call_types.hpp"
#include "config/loader.hpp"
#include "net/event_loop.hpp"
#include "plugins/plugin_abi.hpp"
#include "workspace/types.hpp"

namespace ben_gear {

/// Agent 类型别名 — 指向 agent::runtime::Runtime
using Agent = agent::runtime::Runtime;

} // namespace ben_gear
