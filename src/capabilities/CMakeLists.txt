# 能力模块：工具、技能、MCP

bengear_interface(bengear_capabilities)
target_link_libraries(bengear_capabilities INTERFACE bengear_base bengear_json bengear_workspace)

bengear_module(bengear_tool_core
    tool/types.cpp
    tool/registry.cpp
)
target_link_libraries(bengear_tool_core PUBLIC bengear_base bengear_json)

bengear_module(bengear_tool
    tool/manager.cpp
    tool/builtin_tools.cpp
    tool/workflow_tools.cpp
    tool/history_tools.cpp
    tool/skill_tools.cpp
    tool/memory_tools.cpp
)
target_link_libraries(bengear_tool PUBLIC
    bengear_tool_core
    bengear_workflow
    bengear_workflow_visualizer
    bengear_net
    bengear_skill
    bengear_memory
    bengear_workspace
    bengear_llm
    bengear_application
    bengear_capabilities
)

bengear_module(bengear_acp
    tool/acp/core/content_block.cpp
    tool/acp/core/message.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool_core)

bengear_module(bengear_skill
    skill/skill.cpp
    skill/zip_extract.cpp
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress)

bengear_module(bengear_mcp
    mcp/mcp_client.cpp
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
