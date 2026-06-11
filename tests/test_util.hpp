#pragma once

#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <string>

namespace bengear::test {

/// 创建唯一临时目录（函数式，不依赖 fixture）
inline std::filesystem::path make_tmp_dir(const std::string& suite, const std::string& name) {
    auto dir = std::filesystem::temp_directory_path()
             / ("bengear-" + suite + "-" + name);
    std::filesystem::create_directories(dir);
    return dir;
}

/// 清理临时目录
inline void remove_tmp_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

}  // namespace bengear::test
