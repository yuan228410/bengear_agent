#pragma once

#include "base/container/string.hpp"
#include "base/container/vector.hpp"
#include "base/log/logger.hpp"
#include "memory/store.hpp"
#include "memory/episode.hpp"
#include "tool/registry.hpp"
#include "tool/types.hpp"
#include "workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册记忆相关工具（不含情景记忆工具）
/// 情景记忆工具需要在 Session 构造后单独注册（因为依赖 Session 的 EpisodeStore）
void register_memory_tools(llm::ToolRegistry& tools,
                                   std::shared_ptr<memory::MemoryStore> memory_store);


/// 注册情景记忆工具（由 Session 构造后调用，因为依赖 Session 的 EpisodeStore）
void register_episode_tools(llm::ToolRegistry& tools,
                                    std::shared_ptr<memory::EpisodeStore> episode_store);


}  // namespace ben_gear::tools
