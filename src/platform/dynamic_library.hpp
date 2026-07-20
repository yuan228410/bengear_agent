#pragma once

/// 跨平台动态库加载抽象
///
/// 收敛 LoadLibrary/dlopen 差异，其他模块不再直接使用平台 API

#include "platform/os.hpp"

#include <cstdint>
#include <string>

namespace ben_gear::base::platform {

/// 动态库句柄
#if BEN_GEAR_PLATFORM_WINDOWS
using SharedLibraryHandle = void*;  // HMODULE
#else
using SharedLibraryHandle = void*;
#endif

/// 加载动态库
/// @param path 库文件路径
/// @return 句柄，失败返回 nullptr
SharedLibraryHandle shared_library_load(const char* path);

/// 卸载动态库
void shared_library_unload(SharedLibraryHandle handle);

/// 获取函数符号
/// @param handle 库句柄
/// @param name 符号名称
/// @return 函数指针，失败返回 nullptr
void* shared_library_symbol(SharedLibraryHandle handle, const char* name);

/// 获取最后的错误信息
std::string shared_library_error();

/// 获取插件文件扩展名（平台相关）
inline const char* plugin_extension() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ".dll";
#else
    return ".so";
#endif
}

}  // namespace ben_gear::base::platform
