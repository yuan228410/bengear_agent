#pragma once
// 兼容层：platform.hpp 已合并到 os.hpp
// 新代码应直接 #include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/platform/os.hpp"

namespace ben_gear::support {

inline std::optional<std::string> getenv_string(std::string_view name) {
    return base::platform::os::getenv_optional(std::string(name));
}

inline std::filesystem::path home_directory() {
    return base::platform::os::home_directory();
}

inline std::filesystem::path data_directory() {
    return base::platform::os::data_directory();
}

}  // namespace ben_gear::support

namespace ben_gear {
using support::data_directory;
using support::getenv_string;
using support::home_directory;
}  // namespace ben_gear
