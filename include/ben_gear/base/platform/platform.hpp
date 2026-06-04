#pragma once
// 兼容层：platform.hpp 已合并到 os.hpp
// 新代码应直接 #include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/platform/os.hpp"

namespace ben_gear::support {

// 保留旧接口兼容，委托给 os::
// 注意：旧版返回 std::filesystem::path，新版 os:: 返回 std::string
// 调用方用 / 运算符拼路径时需要包装

inline std::optional<std::string> getenv_string(std::string_view name) {
    return base::platform::os::getenv_optional(std::string(name));
}

inline std::filesystem::path home_directory() {
    return base::platform::os::home_directory();
}

inline std::filesystem::path config_directory() {
    return base::platform::os::config_directory();
}

}  // namespace ben_gear::support

namespace ben_gear {
using support::config_directory;
using support::getenv_string;
using support::home_directory;
}  // namespace ben_gear
