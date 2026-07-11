# 记忆与上下文（context 相关实现已并入本模块）

bengear_module(bengear_memory
    ${BENGEAR_ROOT}/src/memory/section_merge.cpp
    ${BENGEAR_ROOT}/src/memory/store.cpp
    ${BENGEAR_ROOT}/src/memory/episode.cpp
    ${BENGEAR_ROOT}/src/memory/updater.cpp
    ${BENGEAR_ROOT}/src/memory/context.cpp
    ${BENGEAR_ROOT}/src/memory/context_pruner.cpp
    ${BENGEAR_ROOT}/src/memory/compactor.cpp
)
target_link_libraries(bengear_memory PUBLIC bengear_base bengear_json bengear_acp bengear_workspace)
