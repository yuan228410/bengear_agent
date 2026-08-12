# 编排

bengear_module(bengear_orchestration
    plan.cpp
    todo.cpp
    todo_tools.cpp
    plan_parser.cpp
    serializer.cpp
)
target_link_libraries(bengear_orchestration PUBLIC bengear_base bengear_json bengear_llm)
