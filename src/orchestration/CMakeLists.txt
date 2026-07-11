# 编排

bengear_module(bengear_orchestration
    ${BENGEAR_ROOT}/src/orchestration/plan.cpp
    ${BENGEAR_ROOT}/src/orchestration/store.cpp
    ${BENGEAR_ROOT}/src/orchestration/todo.cpp
    ${BENGEAR_ROOT}/src/orchestration/plan_parser.cpp
    ${BENGEAR_ROOT}/src/orchestration/serializer.cpp
)
target_link_libraries(bengear_orchestration PUBLIC bengear_base bengear_json bengear_llm)
