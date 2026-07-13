# CLI 层：渲染、REPL、应用入口

bengear_module(bengear_cli_render
    render/renderer.cpp
    render/agent_event_sink_adapter.cpp
    render/cli_app.cpp
    render/runtime_presenter.cpp
    render/markdown.cpp
    render/highlight.cpp
)
target_link_libraries(bengear_cli_render PUBLIC
    bengear_base
    bengear_json
    bengear_agent_core
    bengear_agent_runtime
    bengear_cli_runtime
    bengear_application
)

bengear_module(bengear_cli_repl
    repl/chat_repl.cpp
    repl/slash_command_dispatcher.cpp
    repl/history_store.cpp
    repl/line_editor.cpp
    repl/terminal_io.cpp
    repl/input_buffer.cpp
)
target_link_libraries(bengear_cli_repl PUBLIC
    bengear_cli_render
    bengear_cli_runtime
    bengear_base
)
target_compile_definitions(bengear_cli_repl PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")

bengear_module(bengear_cli_app
    app.cpp
    app_commands.cpp
    session_runner.cpp
)
target_link_libraries(bengear_cli_app PUBLIC
    bengear_cli_repl
    bengear_server_composition
)
