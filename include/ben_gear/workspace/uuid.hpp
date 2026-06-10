#pragma once

#include "ben_gear/base/container/string.hpp"

namespace ben_gear::workspace {

namespace container = base::container;

/// 生成会话 ID（16 位十六进制，碰撞概率约 10^19 分之一）
container::String generate_uuid();

}  // namespace ben_gear::workspace
