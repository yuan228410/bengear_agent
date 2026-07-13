#pragma once

#include "base/domain/result.hpp"
#include "base/platform/dynamic_library.hpp"
#include "plugins/plugin_abi.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <memory>

namespace ben_gear::plugins {

/// 一个已加载插件的句柄（包含工具数组引用）
struct LoadedPlugin {
    base::platform::SharedLibraryHandle handle = nullptr;
    std::string info_json;           // plugin_info() 返回的元数据
    std::vector<BenGearTool> tools;  // ben_gear_plugin_tools() 返回的工具列表
};

/// 插件加载器 — 扫描目录加载共享库
class PluginLoader {
public:
    explicit PluginLoader(std::filesystem::path plugins_dir = {})
        : plugins_dir_(std::move(plugins_dir)) {}

    /// 扫描并加载所有插件
    /// 返回 (成功加载的插件数, 错误信息列表)
    std::pair<size_t, std::vector<std::string>> load_all();

    /// 卸载所有已加载插件
    void unload_all();

    /// 已加载的插件列表（含工具定义，供 Runtime 注册到 ToolRegistry）
    const std::vector<LoadedPlugin>& loaded_plugins() const { return loaded_plugins_; }

    ~PluginLoader() { unload_all(); }

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&) = default;
    PluginLoader& operator=(PluginLoader&&) = default;

private:
    std::filesystem::path plugins_dir_;
    std::vector<LoadedPlugin> loaded_plugins_;

    domain::AppResult<void> load_plugin(const std::filesystem::path& path);
};

} // namespace ben_gear::plugins
