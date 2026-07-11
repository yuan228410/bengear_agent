# 工作区

bengear_module(bengear_workspace
    ${BENGEAR_ROOT}/src/workspace/uuid.cpp
    ${BENGEAR_ROOT}/src/workspace/history_db.cpp
    ${BENGEAR_ROOT}/src/workspace/history_exporter.cpp
    ${BENGEAR_ROOT}/src/workspace/manager.cpp
    ${BENGEAR_ROOT}/src/workspace/conversation_history.cpp
    ${BENGEAR_ROOT}/src/workspace/session.cpp
)
target_link_libraries(bengear_workspace PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_llm
    bengear_memory
)
