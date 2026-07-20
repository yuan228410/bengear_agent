bengear_module(bengear_tool_core
    tool/types.cpp
    tool/registry.cpp
)
target_link_libraries(bengear_tool_core PUBLIC bengear_base bengear_json)

bengear_module(bengear_tool
    tool/manager.cpp
    tool/builtin_tools.cpp
    tool/file_tools.cpp
    tool/shell_tools.cpp
    tool/http_tools.cpp
    tool/extended_tools.cpp
    tool/replace_tools.cpp
    tool/search_content_tools.cpp
    tool/env_tools.cpp
    tool/image_tools.cpp
)
target_link_libraries(bengear_tool PUBLIC
    bengear_tool_core
)
target_link_libraries(bengear_tool PRIVATE
    bengear_net
)

bengear_module(bengear_skill
    skill/skill.cpp
    skill/zip_extract.cpp
    skill/skill_tool_registration.cpp
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress bengear_tool_core)

bengear_module(bengear_mcp
    mcp/mcp_client.cpp
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
