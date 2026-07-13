#include "plugins/plugin_loader.hpp"

#include "base/log/logger.hpp"
#include "base/container/string.hpp"

#include <filesystem>
#include <system_error>
#include <cstring>

namespace ben_gear::plugins {

namespace container = base::container;
namespace platform = base::platform;

std::pair<size_t, std::vector<std::string>> PluginLoader::load_all() {
    if (plugins_dir_.empty() || !std::filesystem::exists(plugins_dir_)) {
        log::info_fmt("plugin loader: dir not found or empty: {}", plugins_dir_.string());
        return {0, {}};
    }

    size_t loaded = 0;
    std::vector<std::string> errors;

    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir_)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        if (path.extension().string() != platform::plugin_extension()) continue;

        auto result = load_plugin(path);
        if (result.ok()) {
            ++loaded;
            log::info_fmt("plugin loaded: {}", path.filename().string());
        } else {
            errors.push_back(path.filename().string() + ": " + result.error().message);
        }
    }

    log::info_fmt("plugin loader: loaded {} plugins, {} errors", loaded, errors.size());
    return {loaded, errors};
}

void PluginLoader::unload_all() {
    for (auto handle : loaded_plugins_) {
        // 调用可选的 plugin_shutdown
        auto shutdown_fn = reinterpret_cast<PluginShutdownFn>(
            platform::shared_library_symbol(handle, "ben_gear_plugin_shutdown"));
        if (shutdown_fn) {
            try { shutdown_fn(); } catch (...) {}
        }
        unload_plugin(handle);
    }
    loaded_plugins_.clear();
    loaded_metas_.clear();
}

domain::AppResult<void> PluginLoader::load_plugin(const std::filesystem::path& path) {
    auto handle = platform::shared_library_load(path.string().c_str());

    if (!handle) {
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("plugin_load_failed"),
            container::String(("LoadLibrary failed: " + platform::shared_library_error()).c_str())));
    }

    // 查找必需的初始化函数
    auto init_fn = reinterpret_cast<PluginInitFn>(
        platform::shared_library_symbol(handle, "ben_gear_plugin_init"));
    if (!init_fn) {
        platform::shared_library_unload(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("missing_init_fn"), container::String("ben_gear_plugin_init not found")));
    }

    // 收集可选的元数据
    auto info_fn = reinterpret_cast<PluginInfoFn>(
        platform::shared_library_symbol(handle, "plugin_info"));
    if (info_fn) {
        try {
            loaded_metas_.push_back(info_fn());
        } catch (...) {
            // 元数据收集失败不阻止加载
        }
    }

    try {
        init_fn();
    } catch (const std::exception& e) {
        platform::shared_library_unload(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("init_exception"), container::String(e.what())));
    }

    loaded_plugins_.push_back(handle);
    return domain::AppResult<void>::success();
}

void PluginLoader::unload_plugin(platform::SharedLibraryHandle handle) {
    if (!handle) return;
    platform::shared_library_unload(handle);
}

} // namespace ben_gear::plugins
