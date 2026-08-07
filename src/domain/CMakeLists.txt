# 领域事件与错误类型（位于 base 层之上）
# .cpp 实现依赖 acp 和 llm 的类型定义，使用 PRIVATE 避免 PUBLIC 泄漏
bengear_module(bengear_domain
    event.cpp
)
target_link_libraries(bengear_domain PUBLIC bengear_base bengear_json
                                       PRIVATE bengear_acp bengear_llm)
