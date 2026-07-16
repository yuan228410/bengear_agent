# 能力模块：工具、技能、MCP

bengear_interface(bengear_capabilities)
target_link_libraries(bengear_capabilities INTERFACE bengear_base bengear_json bengear_workspace)

# ── 工具与协议层（原 tool/ 目录）─────────────────────────────────
# bengear_tool_core: 类型 + 注册表（无向上依赖，workflow 等模块可依赖）
# bengear_tool: 工具实现（包装各层服务为 LLM 工具）

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
    tool/repo_map_tools.cpp
    tool/code_intel_tools.cpp
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
    bengear_repo_map
    bengear_code_intel
    bengear_workspace_index
)

bengear_module(bengear_acp
    tool/acp/core/content_block.cpp
    tool/acp/core/message.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool_core)

# ── 技能加载器（原 skill/ 目录）───────────────────────────────────

bengear_module(bengear_skill
    skill/skill.cpp
    skill/zip_extract.cpp
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress)

# ── MCP 客户端（原 mcp/ 目录）────────────────────────────────────

bengear_module(bengear_mcp
    mcp/mcp_client.cpp
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
