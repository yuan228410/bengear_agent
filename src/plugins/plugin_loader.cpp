#include "plugins/plugin_loader.hpp"

#include "base/log/logger.hpp"
#include "base/container/string.hpp"

#include <filesystem>
#include <system_error>

namespace ben_gear::plugins {

namespace container = base::container;

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
        std::string ext = path.extension().string();
#if defined(_WIN32)
        if (ext != ".dll") continue;
#else
        if (ext != ".so") continue;
#endif

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
        unload_plugin(handle);
    }
    loaded_plugins_.clear();
}

domain::AppResult<void> PluginLoader::load_plugin(const std::filesystem::path& path) {
    PluginHandle handle = nullptr;
    std::string load_error;

#if defined(_WIN32)
    handle = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        DWORD err = GetLastError();
        load_error = "LoadLibraryExW failed: " + std::to_string(err);
    }
#else
    handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        load_error = dlerror() ? dlerror() : "unknown dlopen error";
    }
#endif

    if (!handle) {
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("plugin_load_failed"), container::String(load_error)));
    }

    // 查找并调用初始化函数
    PluginInitFn init_fn = nullptr;
#if defined(_WIN32)
    init_fn = reinterpret_cast<PluginInitFn>(GetProcAddress(handle, "ben_gear_plugin_init"));
    if (!init_fn) {
        FreeLibrary(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("missing_init_fn"), container::String("ben_gear_plugin_init not found")));
    }
#else
    init_fn = reinterpret_cast<PluginInitFn>(dlsym(handle, "ben_gear_plugin_init"));
    if (!init_fn) {
        dlclose(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("missing_init_fn"), container::String("ben_gear_plugin_init not found")));
    }
#endif

    try {
        init_fn(); // 调用插件初始化，内部应通过 BEN_GEAR_REGISTER_CAPABILITY 注册 capability
    } catch (const std::exception& e) {
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return domain::AppResult<void>::failure(domain::AppError::internal(
            container::String("init_exception"), container::String(e.what())));
    }

    loaded_plugins_.push_back(handle);
    return domain::AppResult<void>::success();
}

void PluginLoader::unload_plugin(PluginHandle handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

} // namespace ben_gear::plugins