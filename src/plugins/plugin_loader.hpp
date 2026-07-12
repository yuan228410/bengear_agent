#pragma once

#include "base/domain/result.hpp"
#include "base/platform/dynamic_library.hpp"
#include "capabilities/capability_registry.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <memory>

namespace ben_gear::plugins {

/// 插件加载器 — 扫描目录加载共享库，每个库在加载时通过 CapabilityRegistrar
/// 自动注册其提供的 Capability
class PluginLoader {
public:
    explicit PluginLoader(std::filesystem::path plugins_dir = {})
        : plugins_dir_(std::move(plugins_dir)) {}

    /// 扫描并加载所有插件
    /// 返回 (成功加载的插件数, 错误信息列表)
    std::pair<size_t, std::vector<std::string>> load_all();

    /// 卸载所有已加载插件
    void unload_all();

    ~PluginLoader() { unload_all(); }

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&) = default;
    PluginLoader& operator=(PluginLoader&&) = default;

private:
    std::filesystem::path plugins_dir_;
    std::vector<base::platform::SharedLibraryHandle> loaded_plugins_;

    /// 加载单个插件文件
    domain::AppResult<void> load_plugin(const std::filesystem::path& path);

    /// 卸载单个插件
    void unload_plugin(base::platform::SharedLibraryHandle handle);
};

/// 插件导出的初始化函数签名
/// 插件需定义: extern "C" void ben_gear_plugin_init();
using PluginInitFn = void(*)();

} // namespace ben_gear::plugins
