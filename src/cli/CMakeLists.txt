# CLI 层：渲染、REPL、应用入口，统一为一个功能模块

bengear_module(bengear_cli_render
    ${BENGEAR_ROOT}/src/cli/render/renderer.cpp
    ${BENGEAR_ROOT}/src/cli/render/agent_event_sink_adapter.cpp
    ${BENGEAR_ROOT}/src/cli/render/cli_app.cpp
    ${BENGEAR_ROOT}/src/cli/render/runtime_presenter.cpp
)
target_link_libraries(bengear_cli_render PUBLIC
    bengear_base
    bengear_json
    bengear_agent_runtime
    bengear_cli_runtime
    bengear_application_runtime_execution
)

bengear_module(bengear_cli_repl
    ${BENGEAR_ROOT}/src/cli/repl/chat_repl.cpp
    ${BENGEAR_ROOT}/src/cli/repl/slash_command_dispatcher.cpp
    ${BENGEAR_ROOT}/src/cli/repl/history_store.cpp
    ${BENGEAR_ROOT}/src/cli/repl/line_editor.cpp
    ${BENGEAR_ROOT}/src/cli/repl/terminal_io.cpp
)
target_link_libraries(bengear_cli_repl PUBLIC
    bengear_cli_render
    bengear_cli_runtime
    bengear_base
)

bengear_module(bengear_cli_app
    ${BENGEAR_ROOT}/src/cli/app.cpp
    ${BENGEAR_ROOT}/src/cli/app_commands.cpp
    ${BENGEAR_ROOT}/src/cli/session_runner.cpp
)
target_link_libraries(bengear_cli_app PUBLIC
    bengear_cli_repl
    bengear_server_event_sink
)
