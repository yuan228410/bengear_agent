#pragma once

// BenGear 插件 ABI 约定（C 兼容，跨平台）

namespace ben_gear::plugins {

/// 工具描述符（插件导出，宿主只读）
struct BenGearTool {
    const char* name;             // 工具名（LLM 可见）
    const char* description;      // 功能描述
    const char* params_json;      // 参数定义 JSON: [{"name":"x","type":"string","description":"...","required":true},...]
    const char* (*execute)(const char* args_json);  // 执行函数
};

/// 插件导出的工具注册函数类型（C linkage）
using PluginToolsFn = const BenGearTool*(*)(int* out_count);

} // namespace ben_gear::plugins

// C ABI 导出宏
#if defined(_WIN32)
#define BEN_GEAR_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define BEN_GEAR_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
