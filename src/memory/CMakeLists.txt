# 记忆与上下文（context 相关实现已并入本模块）

bengear_module(bengear_memory
    section_merge.cpp
    store.cpp
    episode.cpp
    updater.cpp
    context.cpp
    context_pruner.cpp
    compactor.cpp
)
target_link_libraries(bengear_memory PUBLIC bengear_base bengear_json bengear_acp bengear_workspace)
