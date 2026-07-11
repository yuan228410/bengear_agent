# ==================== BenGear 功能模块 ====================
# 每个文件是一个功能模块，按功能聚合（而非按子目录拆分），
# 详见各 src/<module>/CMakeLists.txt。CMake 支持目标前向引用，顺序不强制。

# 基础层
add_subdirectory(base)          # bengear_base / bengear_json / bengear_compress / bengear_core / bengear_domain
add_subdirectory(net)           # bengear_net
add_subdirectory(tool)          # bengear_tool / bengear_acp
add_subdirectory(llm)           # bengear_llm / bengear_skill / bengear_mcp
add_subdirectory(capabilities)  # 权限/审计/Git/检查点/测试循环/补丁
add_subdirectory(memory)        # bengear_memory (+context)
add_subdirectory(workspace)     # bengear_workspace
add_subdirectory(orchestration) # bengear_orchestration
add_subdirectory(workflow)      # bengear_workflow / bengear_workflow_visualizer
add_subdirectory(intelligence)  # 索引/仓库地图/代码智能/诊断
add_subdirectory(application)   # bengear_application
add_subdirectory(agent)         # bengear_agent
add_subdirectory(server)        # HTTP / WS / API / 组合 / 核心
add_subdirectory(cli)           # 渲染 / REPL / 应用入口

# ==================== 聚合 / 接口模块 ====================
# 以下仅做依赖聚合或向后兼容别名，不编译源文件。

# 智能体运行时聚合（agent_core / sub_agent / shared_resources 已合并入 bengear_agent）
bengear_interface(bengear_agent_runtime)
target_link_libraries(bengear_agent_runtime INTERFACE bengear_agent)

# 诊断聚合
bengear_interface(bengear_diagnostic)
target_link_libraries(bengear_diagnostic INTERFACE
    bengear_diagnostic_context
    bengear_diagnostic_repair
)

# 服务端聚合
bengear_interface(bengear_server)
target_link_libraries(bengear_server INTERFACE
    bengear_server_http
    bengear_server_ws
    bengear_server_api
    bengear_server_composition
    bengear_server_core
)

# CLI 运行时聚合
bengear_interface(bengear_cli_runtime)
target_link_libraries(bengear_cli_runtime INTERFACE
    bengear_agent_runtime
    bengear_server
    bengear_workflow
    bengear_workflow_visualizer
)

# 测试运行时聚合
bengear_interface(bengear_test_runtime)
target_link_libraries(bengear_test_runtime INTERFACE
    bengear_core
    bengear_cli_render
    bengear_cli_runtime
    bengear_acp_core
    bengear_acp_stream
    bengear_acp_adapter
    bengear_workflow
)

# ==================== 向后兼容别名 ====================
# 旧模块名被合并后，保留接口别名以避免破坏外部引用。

# workflow_core -> workflow（引擎与子模块已合并）
bengear_interface(bengear_workflow_core)
target_link_libraries(bengear_workflow_core INTERFACE bengear_workflow)

# llm_core / llm_adapter / llm_cooldown -> llm
bengear_interface(bengear_llm_core)
target_link_libraries(bengear_llm_core INTERFACE bengear_llm)
bengear_interface(bengear_llm_adapter)
target_link_libraries(bengear_llm_adapter INTERFACE bengear_llm)
bengear_interface(bengear_llm_cooldown)
target_link_libraries(bengear_llm_cooldown INTERFACE bengear_llm)

# context -> memory（context 相关实现已并入 memory 模块）
bengear_interface(bengear_context)
target_link_libraries(bengear_context INTERFACE bengear_memory)

# acp 子聚合
bengear_interface(bengear_acp_core)
target_link_libraries(bengear_acp_core INTERFACE bengear_acp)
bengear_interface(bengear_acp_stream)
target_link_libraries(bengear_acp_stream INTERFACE bengear_acp_core)
bengear_interface(bengear_acp_adapter)
target_link_libraries(bengear_acp_adapter INTERFACE bengear_acp_core bengear_acp_stream bengear_tool)

# application 子模块别名
bengear_interface(bengear_application_runtime_execution)
target_link_libraries(bengear_application_runtime_execution INTERFACE bengear_application)
bengear_interface(bengear_application_command)
target_link_libraries(bengear_application_command INTERFACE bengear_application)
bengear_interface(bengear_application_workspace)
target_link_libraries(bengear_application_workspace INTERFACE bengear_application)
bengear_interface(bengear_application_patch)
target_link_libraries(bengear_application_patch INTERFACE bengear_application)

# server 子模块别名
bengear_interface(bengear_server_event_sink)
target_link_libraries(bengear_server_event_sink INTERFACE bengear_server_composition)
bengear_interface(bengear_server_api_result_presenter)
target_link_libraries(bengear_server_api_result_presenter INTERFACE bengear_server_api)
