#include "base/platform/dynamic_library.hpp"

#include "base/log/logger.hpp"

#if BEN_GEAR_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ben_gear::base::platform {

SharedLibraryHandle shared_library_load(const char* path) {
#ifdef BEN_GEAR_PLATFORM_WINDOWS
    auto* handle = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        log::error_fmt("shared_library_load failed: {} (error={})", path, GetLastError());
    }
    return handle;
#else
    auto* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        log::error_fmt("shared_library_load failed: {} ({})", path, dlerror());
    }
    return handle;
#endif
}

void shared_library_unload(SharedLibraryHandle handle) {
    if (!handle) return;
#ifdef BEN_GEAR_PLATFORM_WINDOWS
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* shared_library_symbol(SharedLibraryHandle handle, const char* name) {
    if (!handle) return nullptr;
#ifdef BEN_GEAR_PLATFORM_WINDOWS
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string shared_library_error() {
#ifdef BEN_GEAR_PLATFORM_WINDOWS
    return "error code: " + std::to_string(GetLastError());
#else
    const char* err = dlerror();
    return err ? err : "unknown error";
#endif
}

}  // namespace ben_gear::base::platform
