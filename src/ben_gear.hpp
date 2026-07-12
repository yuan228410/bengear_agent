#pragma once

#include "agent/core/interface/agent_core.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "agent/plugins/interface/agent_plugins.hpp"
#include "agent/runtime/runtime.hpp"
#include "base/config/loader.hpp"
#include "net/event_loop.hpp"
#include "workspace/types.hpp"
#include "workspace/session.hpp"
#include "workspace/history_db.hpp"

namespace ben_gear {

/// Agent 类型别名 — 指向 agent::runtime::Runtime
using Agent = agent::runtime::Runtime;

} // namespace ben_gear
