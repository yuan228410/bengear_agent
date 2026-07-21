#pragma once

// BenGear 预编译头（PCH）
// 仅包含全项目高频使用的 STL 头文件（每个都经预处理行数/引用频率验证）。
// 低频重型头文件（chrono/filesystem/thread/mutex/sstream/fstream 等）由各 .cpp 按需包含，
// 避免 PCH 膨胀至 100MB+。

// 10 个高频 STL 头文件 — 全项目 90%+ 的 .cpp 都需要
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
