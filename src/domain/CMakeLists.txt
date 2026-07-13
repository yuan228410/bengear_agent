# 领域事件与错误类型（位于 base 层之上，依赖 tool/llm 层）

bengear_module(bengear_domain
    event.cpp
)
target_link_libraries(bengear_domain PUBLIC bengear_base bengear_json bengear_tool)
