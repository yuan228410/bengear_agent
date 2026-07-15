#pragma once

// BenGear 预编译头（PCH）
// 仅包含稳定、跨模块高频使用的 base 层与 STL 头文件，避免引入上层依赖（tool/server/llm 等），
// 以保证分层边界清晰，同时最大化编译加速收益。每个功能库通过 bengear_module() 统一挂载。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "domain/result.hpp"
#include "base/log/logger.hpp"
#include "base/utils/json.hpp"
