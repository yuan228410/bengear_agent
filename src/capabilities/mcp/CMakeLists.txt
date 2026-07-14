# MCP 客户端
bengear_module(bengear_mcp
    mcp_client.cpp
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
