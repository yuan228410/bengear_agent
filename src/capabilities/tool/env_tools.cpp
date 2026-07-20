#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "platform/os.hpp"
#include "base/utils/json.hpp"

#include <string>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_env_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("env_get"),
        std::string("Get an environment variable. Returns null if not set."),
        {{std::string("name"), {std::string("string"), std::string("Variable name")}}},
        [](const Json& args) -> std::string {
            std::string name = args.at("name").get<std::string>();
            const char* val = std::getenv(name.c_str());
            return std::string(Json{{"name", name}, {"exists", val != nullptr},
                {"value", val ? val : ""}}.dump().c_str());
        }
    );

    registry.register_tool(
        std::string("env_set"),
        std::string("Set an environment variable for this session (not persistent)."),
        {
            {std::string("name"), {std::string("string"), std::string("Variable name")}},
            {std::string("value"), {std::string("string"), std::string("Variable value")}}
        },
        [](const Json& args) -> std::string {
            std::string name = args.at("name").get<std::string>();
            std::string value = args.at("value").get<std::string>();
            base::platform::compat::setenv_c(name.c_str(), value.c_str(), 1);
            return Json{{"success", true}, {"name", name}}.dump();
        }
    );
}

} // namespace ben_gear::tools
