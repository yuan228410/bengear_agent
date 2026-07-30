# 工作空间核心（纯 CRUD，不依赖 LLM / memory）
bengear_module(bengear_workspace_core
    history_db.cpp
    history_db_sessions.cpp
    history_db_episodes.cpp
    history_db_search.cpp
    history_db_state.cpp
    history_exporter.cpp
    manager.cpp
    resolver.cpp
)
target_link_libraries(bengear_workspace_core PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool_core
    bengear_tool
    bengear_domain
)
target_link_libraries(bengear_workspace_core PRIVATE sqlite3)

# 工作空间完整模块（会话 + 历史工具注册，依赖 LLM / memory）
bengear_module(bengear_workspace
    session.cpp
    history_tool_registration.cpp
)
target_link_libraries(bengear_workspace PUBLIC
    bengear_workspace_core
    bengear_llm
    bengear_memory
)
