# 能力模块：权限、审计、Git、检查点、测试循环、补丁

bengear_module(bengear_permission
    permission/policy_engine.cpp
)
target_link_libraries(bengear_permission PUBLIC bengear_base bengear_json)

bengear_module(bengear_audit
    audit/audit_store.cpp
)
target_link_libraries(bengear_audit PUBLIC bengear_base bengear_json)

bengear_module(bengear_git
    git/git_service.cpp
)
target_link_libraries(bengear_git PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_checkpoint
    checkpoint/checkpoint_service.cpp
)
target_link_libraries(bengear_checkpoint PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_test_loop
    test_loop/diagnostics.cpp
    test_loop/test_loop_service.cpp
)
target_link_libraries(bengear_test_loop PUBLIC bengear_base bengear_json bengear_domain)

bengear_module(bengear_patch
    patch/diff_parser.cpp
    patch/change_store.cpp
    patch/patch_service.cpp
)
target_link_libraries(bengear_patch PUBLIC bengear_base bengear_json bengear_domain bengear_workspace)
