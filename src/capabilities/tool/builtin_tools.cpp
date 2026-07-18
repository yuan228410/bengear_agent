#include "capabilities/tool/builtin_tools.hpp"

namespace ben_gear::tools {

void register_builtin_tools(capabilities::tool::ToolRegistry& registry, int command_timeout) {
    register_file_tools(registry);
    register_shell_tools(registry, command_timeout);
    register_extended_tools(registry);
    register_replace_tools(registry);
    register_search_content_tools(registry);
    register_env_tools(registry);
    register_image_tools(registry);
}

} // namespace ben_gear::tools
