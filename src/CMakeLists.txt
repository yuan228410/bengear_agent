# ==================== BenGear 功能模块 ====================
# 每个子目录是一个功能模块（头文件与 .cpp 同目录，单树布局），
# CMakeLists 使用相对路径引用本目录源文件，详见各 src/<module>/CMakeLists.txt。
# CMake 支持目标前向引用，add_subdirectory 顺序不强制。

add_subdirectory(base)          # bengear_base / bengear_json / bengear_compress / bengear_core / 叶子库
add_subdirectory(domain)        # bengear_domain（事件/错误）
add_subdirectory(llm)           # bengear_llm（Provider 客户端）
add_subdirectory(capabilities)  # tool/skill/mcp/权限/Git/检查点/测试循环/补丁/插件
add_subdirectory(plugins)       # 插件加载器
add_subdirectory(memory)        # bengear_memory
add_subdirectory(workspace)     # bengear_workspace
add_subdirectory(orchestration) # bengear_orchestration
add_subdirectory(workflow)      # bengear_workflow / bengear_workflow_visualizer
add_subdirectory(application)   # bengear_application
add_subdirectory(agent)         # bengear_agent_core / bengear_agent_plugins / bengear_agent_runtime
add_subdirectory(server)        # HTTP / WS / API / 组合 / 核心
add_subdirectory(cli)           # 渲染 / REPL / 应用入口

# ── 聚合接口 ──────────────────────────────────────────────────────

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
    bengear_agent_core
    bengear_server
    bengear_workflow
    bengear_workflow_visualizer
)

# 测试运行时聚合
bengear_interface(bengear_test_runtime)
target_link_libraries(bengear_test_runtime INTERFACE
    bengear_agent_runtime
    bengear_agent_core
    bengear_server
    bengear_cli_render
    bengear_cli_runtime
    bengear_workflow
    bengear_workflow_visualizer
)
