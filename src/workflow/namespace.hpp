#pragma once

#include <string>

namespace ben_gear::workflow {

/// 工作流命名空间常量
/// 命名空间用于按 (用户, 工作空间, 会话) 隔离工作流定义。
/// 命名空间应作为显式参数传递，而非依赖 thread_local 隐式上下文。
constexpr const char* kDefaultNamespace = "";

}  // namespace ben_gear::workflow
