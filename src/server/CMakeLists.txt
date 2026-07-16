# 服务端：HTTP / WS / API / 组合 / 核心

bengear_module(bengear_server_http
    http/parser.cpp
    http/static_files.cpp
    auth/auth.cpp
    core/router.cpp
)
target_link_libraries(bengear_server_http PUBLIC bengear_base bengear_json bengear_net)

bengear_module(bengear_server_ws
    ws/protocol.cpp
    ws/handler.cpp
    ws/session_message_dispatcher.cpp
    ws/session_message_handlers.cpp
)
target_link_libraries(bengear_server_ws PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_orchestration
    bengear_agent_runtime
    bengear_server_composition
)
if(WIN32)
    target_link_libraries(bengear_server_ws PUBLIC bcrypt)
endif()

bengear_module(bengear_server_api
    api/handlers.cpp
    api/result_presenter.cpp
    api/session_api.cpp
    api/config_api.cpp
    api/mcp_api.cpp
    api/file_api.cpp
)
target_link_libraries(bengear_server_api PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_orchestration
    bengear_workflow
    bengear_mcp
)

bengear_module(bengear_server_composition
    composition/application_services.cpp
    composition/basic_api_composition.cpp
    composition/command_api_composition.cpp
    composition/server_composition.cpp
    callback/server_event_sink.cpp
    callback/workflow_event_projection.cpp
)
target_link_libraries(bengear_server_composition PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_orchestration
    bengear_workflow
    bengear_server_api
    bengear_server_ws
    bengear_server_http
)
target_compile_definitions(bengear_server_composition PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")

bengear_module(bengear_server_core
    core/server.cpp
    core/server_lifecycle.cpp
    session/pool.cpp
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
    bengear_agent_core
    bengear_agent_runtime
    bengear_server_api
    bengear_server_ws
    bengear_server_http
    bengear_server_composition
)
