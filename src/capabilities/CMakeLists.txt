# 能力模块：工具、技能、MCP、权限、审计、Git、检查点、测试循环、补丁

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
    tool/permission_tools.cpp
    tool/patch_tools.cpp
    tool/git_tools.cpp
    tool/checkpoint_tools.cpp
    tool/test_loop_tools.cpp
    tool/repo_map_tools.cpp
    tool/code_intel_tools.cpp
    tool/diagnostic_context_tools.cpp
    tool/diagnostic_repair_tools.cpp
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
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_patch
    bengear_repo_map
    bengear_code_intel
    bengear_workspace_index
    bengear_diagnostic
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

# ── 其余能力模块 ──────────────────────────────────────────────────

bengear_module(bengear_permission
    permission/policy_engine.cpp
)
target_link_libraries(bengear_permission PUBLIC bengear_base bengear_json)

bengear_module(bengear_audit
    audit/audit_store.cpp
)
target_link_libraries(bengear_audit PUBLIC bengear_base bengear_json)

bengear_module(bengear_git
    git/git_service.cpp
)
target_link_libraries(bengear_git PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_checkpoint
    checkpoint/checkpoint_service.cpp
)
target_link_libraries(bengear_checkpoint PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_test_loop
    test_loop/diagnostics.cpp
    test_loop/test_loop_service.cpp
)
target_link_libraries(bengear_test_loop PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_patch
    patch/diff_parser.cpp
    patch/change_store.cpp
    patch/patch_service.cpp
)
target_link_libraries(bengear_patch PUBLIC bengear_base bengear_json bengear_domain bengear_workspace)
