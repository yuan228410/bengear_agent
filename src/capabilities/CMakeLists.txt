# 能力模块：权限、审计、Git、检查点、测试循环、补丁

bengear_module(bengear_permission
    ${BENGEAR_ROOT}/src/permission/policy_engine.cpp
)
target_link_libraries(bengear_permission PUBLIC bengear_base bengear_json)

bengear_module(bengear_audit
    ${BENGEAR_ROOT}/src/audit/audit_store.cpp
)
target_link_libraries(bengear_audit PUBLIC bengear_base bengear_json)

bengear_module(bengear_git
    ${BENGEAR_ROOT}/src/git/git_service.cpp
)
target_link_libraries(bengear_git PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_checkpoint
    ${BENGEAR_ROOT}/src/checkpoint/checkpoint_service.cpp
)
target_link_libraries(bengear_checkpoint PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_test_loop
    ${BENGEAR_ROOT}/src/test_loop/diagnostics.cpp
    ${BENGEAR_ROOT}/src/test_loop/test_loop_service.cpp
)
target_link_libraries(bengear_test_loop PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_patch
    ${BENGEAR_ROOT}/src/patch/diff_parser.cpp
    ${BENGEAR_ROOT}/src/patch/change_store.cpp
    ${BENGEAR_ROOT}/src/patch/patch_service.cpp
)
target_link_libraries(bengear_patch PUBLIC bengear_base bengear_json bengear_domain bengear_workspace)
