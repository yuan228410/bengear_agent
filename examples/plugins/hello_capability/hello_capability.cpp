#include "plugins/plugin_abi.hpp"

#include <string>

using namespace ben_gear::plugins;

static std::string g_last_result;

static const char* hello_execute(const char* args_json) {
    g_last_result = "Hello from plugin!";
    return g_last_result.c_str();
}

static BenGearTool g_tools[] = {
    {"hello", "Say hello from a dynamically loaded plugin",
     R"([{"name":"name","type":"string","description":"Who to greet","required":false}])",
     hello_execute}
};

BEN_GEAR_PLUGIN_EXPORT const BenGearTool* ben_gear_plugin_tools(int* out_count) {
    *out_count = sizeof(g_tools) / sizeof(g_tools[0]);
    return g_tools;
}

BEN_GEAR_PLUGIN_EXPORT const char* plugin_info() {
    return R"({"name":"hello_capability","version":"1.0.0","description":"Example plugin"})";
}

BEN_GEAR_PLUGIN_EXPORT void ben_gear_plugin_shutdown() {}
