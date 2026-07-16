# 工作区

bengear_module(bengear_workspace
    history_db.cpp
    history_exporter.cpp
    manager.cpp
    session.cpp
    history_tool_registration.cpp
)
target_link_libraries(bengear_workspace PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool_core
    bengear_tool
    bengear_llm
    bengear_memory
)
target_link_libraries(bengear_workspace PRIVATE
    sqlite3
)
