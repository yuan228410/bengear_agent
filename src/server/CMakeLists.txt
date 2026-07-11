# 服务端：HTTP / WS / API / 组合 / 核心，统一为一个功能模块

bengear_module(bengear_server_http
    ${BENGEAR_ROOT}/src/server/http/parser.cpp
    ${BENGEAR_ROOT}/src/server/http/static_files.cpp
    ${BENGEAR_ROOT}/src/server/auth/auth.cpp
    ${BENGEAR_ROOT}/src/server/core/router.cpp
)
target_link_libraries(bengear_server_http PUBLIC bengear_base bengear_json bengear_net)

bengear_module(bengear_server_ws
    ${BENGEAR_ROOT}/src/server/ws/protocol.cpp
    ${BENGEAR_ROOT}/src/server/ws/handler.cpp
    ${BENGEAR_ROOT}/src/server/ws/session_message_dispatcher.cpp
    ${BENGEAR_ROOT}/src/server/ws/permission_message_handler.cpp
    ${BENGEAR_ROOT}/src/server/ws/session_message_handlers.cpp
)
target_link_libraries(bengear_server_ws PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_orchestration
    bengear_permission
    bengear_agent_runtime
    bengear_server_event_sink
)
if(WIN32)
    target_link_libraries(bengear_server_ws PUBLIC bcrypt)
endif()

bengear_module(bengear_server_api
    ${BENGEAR_ROOT}/src/server/api/handlers.cpp
    ${BENGEAR_ROOT}/src/server/api/result_presenter.cpp
    ${BENGEAR_ROOT}/src/server/api/session_api.cpp
    ${BENGEAR_ROOT}/src/server/api/config_api.cpp
    ${BENGEAR_ROOT}/src/server/api/mcp_api.cpp
    ${BENGEAR_ROOT}/src/server/api/file_api.cpp
    ${BENGEAR_ROOT}/src/server/api/git_api.cpp
    ${BENGEAR_ROOT}/src/server/api/permission_api.cpp
    ${BENGEAR_ROOT}/src/server/api/patch_api.cpp
    ${BENGEAR_ROOT}/src/server/api/checkpoint_api.cpp
    ${BENGEAR_ROOT}/src/server/api/test_loop_api.cpp
    ${BENGEAR_ROOT}/src/server/api/audit_api.cpp
    ${BENGEAR_ROOT}/src/server/api/runtime_api.cpp
    ${BENGEAR_ROOT}/src/server/api/repo_map_api.cpp
    ${BENGEAR_ROOT}/src/server/api/code_intel_api.cpp
    ${BENGEAR_ROOT}/src/server/api/workbench_api.cpp
    ${BENGEAR_ROOT}/src/server/api/diagnostic_context_api.cpp
    ${BENGEAR_ROOT}/src/server/api/diagnostic_repair_api.cpp
)
target_link_libraries(bengear_server_api PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_audit
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
    bengear_orchestration
    bengear_workflow
    bengear_mcp
)

bengear_module(bengear_server_composition
    ${BENGEAR_ROOT}/src/server/composition/application_services.cpp
    ${BENGEAR_ROOT}/src/server/composition/basic_api_composition.cpp
    ${BENGEAR_ROOT}/src/server/composition/command_api_composition.cpp
    ${BENGEAR_ROOT}/src/server/composition/server_composition.cpp
    ${BENGEAR_ROOT}/src/server/composition/workbench_contexts.cpp
    ${BENGEAR_ROOT}/src/server/callback/server_event_sink.cpp
    ${BENGEAR_ROOT}/src/server/callback/workflow_event_projection.cpp
)
target_link_libraries(bengear_server_composition PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_audit
    bengear_orchestration
    bengear_workflow
    bengear_server_api
    bengear_server_ws
    bengear_server_http
    bengear_diagnostic_repair
)
target_compile_definitions(bengear_server_composition PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")

bengear_module(bengear_server_core
    ${BENGEAR_ROOT}/src/server/core/server.cpp
    ${BENGEAR_ROOT}/src/server/core/server_lifecycle.cpp
    ${BENGEAR_ROOT}/src/server/session/pool.cpp
)
target_link_libraries(bengear_server_core PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_orchestration
    bengear_workflow
    bengear_agent_runtime
    bengear_server_api
    bengear_server_ws
    bengear_server_http
    bengear_server_composition
    bengear_server_event_sink
)
