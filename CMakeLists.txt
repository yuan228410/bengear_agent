cmake_minimum_required(VERSION 3.20)
project(ben_gear VERSION 0.5.0 LANGUAGES C CXX)

option(BEN_GEAR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(BEN_GEAR_BUILD_TESTS "Build BenGear unit tests" ON)
option(BEN_GEAR_BUILD_EXAMPLES "Build BenGear examples" ON)
option(BEN_GEAR_BUILD_BENCHMARKS "Build BenGear benchmarks" ON)

# ==================== 后端选择 ====================
# TLS: mbedtls (默认, vendor) | openssl (系统) | schannel (Windows) | none
set(TLS_BACKEND "mbedtls" CACHE STRING "TLS backend: openssl|mbedtls|schannel|none")
# 压缩: zlib (默认, vendor) | none
set(COMPRESS_BACKEND "zlib" CACHE STRING "Compress backend: zlib|none")

# ==================== 构建类型说明 ====================
#
# Debug（完整调试符号 + dSYM，崩溃可看行号）：
#   cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-dbg
#
# Release（无调试符号，体积最小性能最好）：
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# RelWithDebInfo（优化 + 调试符号，推荐日常开发）：
#   cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-dbg
#
# 默认构建（不指定 BUILD_TYPE）等效 Release
# ===========================================================

# 默认构建类型为 RelWithDebInfo（优化+符号，兼顾性能和调试）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# ==================== Vendor: SQLite3 ====================

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_THREADSAFE=1
)
target_compile_features(sqlite3 PUBLIC c_std_99)

# ==================== Vendor: zlib ====================

if(COMPRESS_BACKEND STREQUAL "zlib")
    set(ZLIB_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
    add_subdirectory(third_party/zlib)
    set(BENGEAR_COMPRESS_LIBS ZLIB::ZLIBSTATIC)
    set(BENGEAR_COMPRESS_DEFINITIONS BEN_GEAR_COMPRESS_ZLIB)
endif()

# ==================== TLS 后端 ====================

if(TLS_BACKEND STREQUAL "openssl")
    find_package(OpenSSL REQUIRED)
    set(BENGEAR_TLS_SOURCES src/net/tls/openssl_engine.cpp)
    set(BENGEAR_TLS_LIBS OpenSSL::SSL OpenSSL::Crypto)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_OPENSSL)
elseif(TLS_BACKEND STREQUAL "mbedtls")
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/mbedtls)
    set(BENGEAR_TLS_SOURCES src/net/tls/mbed_tls_engine.cpp)
    set(BENGEAR_TLS_LIBS mbedtls mbedcrypto mbedx509)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_MBEDTLS)
elseif(TLS_BACKEND STREQUAL "schannel")
    set(BENGEAR_TLS_SOURCES src/net/tls/schannel_engine.cpp)
    set(BENGEAR_TLS_LIBS secur32 crypt32 bcrypt ncrypt)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_SCHANNEL)
elseif(TLS_BACKEND STREQUAL "none")
    set(BENGEAR_TLS_SOURCES)
    set(BENGEAR_TLS_LIBS)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_NONE)
else()
    # 自动检测
    if(WIN32)
        set(BENGEAR_TLS_SOURCES src/net/tls/schannel_engine.cpp)
        set(BENGEAR_TLS_LIBS secur32 crypt32 bcrypt ncrypt)
        set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_SCHANNEL)
    else()
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/mbedtls)
        set(BENGEAR_TLS_SOURCES src/net/tls/mbed_tls_engine.cpp)
        set(BENGEAR_TLS_LIBS mbedtls mbedcrypto mbedx509)
        set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_MBEDTLS)
    endif()
endif()

# ==================== 工具函数 ====================

function(bengear_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(BEN_GEAR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(BEN_GEAR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# ==================== bengear_json ====================

add_library(bengear_json STATIC
 src/base/json/json.cpp
 src/base/json/json_dom.cpp
 src/base/json/json_parser.cpp
 src/base/json/json_serializer.cpp
 src/base/json/json_simd.cpp
 src/base/memory/pool.cpp
 src/base/container/string.cpp
)

target_compile_features(bengear_json PUBLIC cxx_std_20)
target_include_directories(bengear_json PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/include
 ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
bengear_apply_warnings(bengear_json)

# ==================== bengear_base ====================

add_library(bengear_base STATIC
    src/base/concurrency/thread_pool.cpp
    src/base/platform/platform.cpp
    src/config/loader.cpp
)

target_compile_features(bengear_base PUBLIC cxx_std_20)
target_include_directories(bengear_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_base PUBLIC sqlite3 bengear_json)

# ==================== bengear_compress ====================

add_library(bengear_compress STATIC
    src/compress/compress_engine.cpp
    src/compress/zlib_engine.cpp
)

target_compile_features(bengear_compress PUBLIC cxx_std_20)
target_include_directories(bengear_compress PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
)
target_link_libraries(bengear_compress PUBLIC bengear_base ${BENGEAR_COMPRESS_LIBS})
target_compile_definitions(bengear_compress PUBLIC ${BENGEAR_COMPRESS_DEFINITIONS})

# ==================== bengear_tls ====================

add_library(bengear_tls STATIC
    src/net/tls/tls_engine.cpp
    ${BENGEAR_TLS_SOURCES}
)

target_compile_features(bengear_tls PUBLIC cxx_std_20)
target_include_directories(bengear_tls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls
)
target_link_libraries(bengear_tls PUBLIC bengear_base ${BENGEAR_TLS_LIBS})
target_compile_definitions(bengear_tls PUBLIC ${BENGEAR_TLS_DEFINITIONS})

# ==================== bengear_net ====================

add_library(bengear_net STATIC
    src/base/net/event_loop.cpp
    src/base/net/socket.cpp
    src/base/net/wakeup_fd.cpp
    src/base/net/tcp_stream.cpp
    src/base/net/connection_pool.cpp
)

target_compile_features(bengear_net PUBLIC cxx_std_20)
target_include_directories(bengear_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_net PUBLIC bengear_base bengear_tls bengear_compress)

if(WIN32)
    target_link_libraries(bengear_net PUBLIC ws2_32 mswsock)
endif()

# ==================== bengear_domain ====================

add_library(bengear_domain STATIC
    src/domain/event.cpp
)
target_compile_features(bengear_domain PUBLIC cxx_std_20)
target_include_directories(bengear_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_domain PUBLIC bengear_base bengear_json bengear_tool)
bengear_apply_warnings(bengear_domain)

# ==================== bengear_tool ====================

add_library(bengear_tool STATIC
    src/tool/types.cpp
    src/tool/registry.cpp
    src/tool/manager.cpp
)

target_compile_features(bengear_tool PUBLIC cxx_std_20)
target_include_directories(bengear_tool PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_tool PUBLIC bengear_base bengear_json)

# ==================== bengear_mcp ====================

add_library(bengear_mcp STATIC
    src/mcp/mcp_client.cpp
)
target_compile_features(bengear_mcp PUBLIC cxx_std_20)
target_include_directories(bengear_mcp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
bengear_apply_warnings(bengear_mcp)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")

# ==================== bengear_agent_runtime ====================

add_library(bengear_agent_runtime INTERFACE)
target_compile_features(bengear_agent_runtime INTERFACE cxx_std_20)
target_include_directories(bengear_agent_runtime INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_agent_runtime INTERFACE
    bengear_agent_core
    bengear_agent_shared_resources
    bengear_sub_agent
)


# ==================== bengear_agent_core ====================

add_library(bengear_agent_core STATIC
    src/agent/agent.cpp
)
target_compile_features(bengear_agent_core PUBLIC cxx_std_20)
target_include_directories(bengear_agent_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_agent_core PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_llm_core
)
bengear_apply_warnings(bengear_agent_core)

# ==================== bengear_sub_agent ====================

add_library(bengear_sub_agent STATIC
    src/agent/sub_agent.cpp
    src/tools/sub_agent_tools.cpp
)
target_compile_features(bengear_sub_agent PUBLIC cxx_std_20)
target_include_directories(bengear_sub_agent PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_sub_agent PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_orchestration
    bengear_llm_core
    bengear_agent_core
)
bengear_apply_warnings(bengear_sub_agent)

# ==================== bengear_agent_shared_resources ====================

add_library(bengear_agent_shared_resources STATIC
    src/agent/shared_resources.cpp
)
target_compile_features(bengear_agent_shared_resources PUBLIC cxx_std_20)
target_include_directories(bengear_agent_shared_resources PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_agent_shared_resources PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_skill
    bengear_memory
    bengear_context
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_audit
    bengear_workspace_index
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
    bengear_orchestration
    bengear_workflow_core
    bengear_llm_core
    bengear_application
    bengear_workspace
    bengear_mcp
    bengear_agent_core
)
bengear_apply_warnings(bengear_agent_shared_resources)

# ==================== bengear_server_git_api ====================

add_library(bengear_server_git_api STATIC
    src/server/api/git_api.cpp
)
target_compile_features(bengear_server_git_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_git_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_git_api PUBLIC bengear_base bengear_json bengear_net bengear_domain bengear_workspace bengear_git)
bengear_apply_warnings(bengear_server_git_api)

# ==================== bengear_server_permission_api ====================

add_library(bengear_server_permission_api STATIC
    src/server/api/permission_api.cpp
)
target_compile_features(bengear_server_permission_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_permission_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_permission_api PUBLIC bengear_base bengear_json bengear_net bengear_domain bengear_workspace bengear_permission)
bengear_apply_warnings(bengear_server_permission_api)

# ==================== bengear_server_patch_api ====================

add_library(bengear_server_patch_api STATIC
    src/server/api/patch_api.cpp
)
target_compile_features(bengear_server_patch_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_patch_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_patch_api PUBLIC bengear_base bengear_json bengear_net bengear_domain bengear_workspace bengear_application bengear_patch)
bengear_apply_warnings(bengear_server_patch_api)

# ==================== bengear_server_checkpoint_api ====================

add_library(bengear_server_checkpoint_api STATIC
    src/server/api/checkpoint_api.cpp
)
target_compile_features(bengear_server_checkpoint_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_checkpoint_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_checkpoint_api PUBLIC bengear_base bengear_json bengear_net bengear_domain bengear_workspace bengear_checkpoint)
bengear_apply_warnings(bengear_server_checkpoint_api)

# ==================== bengear_server_test_loop_api ====================

add_library(bengear_server_test_loop_api STATIC
    src/server/api/test_loop_api.cpp
)
target_compile_features(bengear_server_test_loop_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_test_loop_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_test_loop_api PUBLIC bengear_base bengear_json bengear_net bengear_domain bengear_workspace bengear_test_loop)
bengear_apply_warnings(bengear_server_test_loop_api)

# ==================== bengear_server_command_api ====================

add_library(bengear_server_command_api INTERFACE)
target_compile_features(bengear_server_command_api INTERFACE cxx_std_20)
target_include_directories(bengear_server_command_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_command_api INTERFACE
    bengear_server_git_api
    bengear_server_permission_api
    bengear_server_patch_api
    bengear_server_checkpoint_api
    bengear_server_test_loop_api
)

# ==================== bengear_server_intelligence_api ====================

add_library(bengear_server_intelligence_api STATIC
    src/server/api/diagnostic_context_api.cpp
    src/server/api/diagnostic_repair_api.cpp
    src/server/api/repo_map_api.cpp
    src/server/api/code_intel_api.cpp
    src/server/api/audit_api.cpp
)
target_compile_features(bengear_server_intelligence_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_intelligence_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_intelligence_api PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_domain
    bengear_audit
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
)
bengear_apply_warnings(bengear_server_intelligence_api)

# ==================== bengear_server_basic_api ====================

add_library(bengear_server_basic_api STATIC
    src/server/api/result_presenter.cpp
    src/server/api/session_api.cpp
    src/server/api/config_api.cpp
    src/server/api/mcp_api.cpp
    src/server/api/file_api.cpp
)
target_compile_features(bengear_server_basic_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_basic_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_basic_api PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_domain
    bengear_workspace
    bengear_mcp
)
bengear_apply_warnings(bengear_server_basic_api)

# ==================== bengear_server_api ====================

add_library(bengear_server_api STATIC
    src/server/api/handlers.cpp
)
target_compile_features(bengear_server_api PUBLIC cxx_std_20)
target_include_directories(bengear_server_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_api PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_audit
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
    bengear_orchestration
    bengear_workflow_core
    bengear_mcp
    bengear_server_command_api
    bengear_server_intelligence_api
    bengear_server_basic_api
)
bengear_apply_warnings(bengear_server_api)

# ==================== bengear_server_ws ====================

add_library(bengear_server_ws STATIC
    src/server/ws/handler.cpp
    src/server/ws/permission_message_handler.cpp
    src/server/ws/protocol.cpp
    src/server/ws/session_message_dispatcher.cpp
    src/server/ws/session_message_handlers.cpp
)
target_compile_features(bengear_server_ws PUBLIC cxx_std_20)
target_include_directories(bengear_server_ws PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_ws PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_permission
    bengear_orchestration
    bengear_workflow_core
    bengear_agent_runtime
)
bengear_apply_warnings(bengear_server_ws)

# ==================== bengear_server_http ====================

add_library(bengear_server_http STATIC
    src/server/core/router.cpp
    src/server/http/static_files.cpp
    src/server/http/parser.cpp
    src/server/auth/auth.cpp
)
target_compile_features(bengear_server_http PUBLIC cxx_std_20)
target_include_directories(bengear_server_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_http PUBLIC
    bengear_base
    bengear_json
    bengear_net
)
bengear_apply_warnings(bengear_server_http)

# ==================== bengear_server_composition ====================

add_library(bengear_server_composition STATIC
    src/server/composition/application_services.cpp
    src/server/composition/basic_api_composition.cpp
    src/server/composition/command_api_composition.cpp
    src/server/composition/server_composition.cpp
)
target_compile_features(bengear_server_composition PUBLIC cxx_std_20)
target_include_directories(bengear_server_composition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_composition PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_application
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_audit
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
    bengear_orchestration
    bengear_workflow_core
    bengear_agent_runtime
    bengear_server_api
    bengear_server_ws
    bengear_server_http
)
bengear_apply_warnings(bengear_server_composition)
target_compile_definitions(bengear_server_composition PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")

# ==================== bengear_server_runtime ====================

add_library(bengear_server_runtime STATIC
    src/server/session/pool.cpp
    src/server/callback/server_event_sink.cpp
    src/server/core/server.cpp
    src/server/core/server_lifecycle.cpp
)
target_compile_features(bengear_server_runtime PUBLIC cxx_std_20)
target_include_directories(bengear_server_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server_runtime PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_orchestration
    bengear_workflow_core
    bengear_agent_runtime
    bengear_server_api
    bengear_server_ws
    bengear_server_http
    bengear_server_composition
)
bengear_apply_warnings(bengear_server_runtime)

# ==================== bengear_server ====================

add_library(bengear_server INTERFACE)
target_compile_features(bengear_server INTERFACE cxx_std_20)
target_include_directories(bengear_server INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_server INTERFACE
    bengear_server_api
    bengear_server_basic_api
    bengear_server_command_api
    bengear_server_intelligence_api
    bengear_server_ws
    bengear_server_http
    bengear_server_composition
    bengear_server_runtime
)


# ==================== bengear_workflow_tasks ====================

add_library(bengear_workflow_tasks STATIC
    src/workflow/task_types.cpp
)
target_compile_features(bengear_workflow_tasks PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_tasks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_tasks PUBLIC bengear_base bengear_json bengear_net bengear_tool)
bengear_apply_warnings(bengear_workflow_tasks)

# ==================== bengear_workflow_executor ====================

add_library(bengear_workflow_executor STATIC
    src/workflow/executor.cpp
)
target_compile_features(bengear_workflow_executor PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_executor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_executor PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_workflow_executor)

# ==================== bengear_workflow_metrics ====================

add_library(bengear_workflow_metrics STATIC
    src/workflow/metrics.cpp
)
target_compile_features(bengear_workflow_metrics PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_metrics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_metrics PUBLIC bengear_base bengear_json bengear_domain)
bengear_apply_warnings(bengear_workflow_metrics)

# ==================== bengear_workflow_approval ====================

add_library(bengear_workflow_approval STATIC
    src/workflow/approval_manager.cpp
)
target_compile_features(bengear_workflow_approval PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_approval PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_approval PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_workflow_approval)

# ==================== bengear_workflow_storage ====================

add_library(bengear_workflow_storage STATIC
    src/workflow/storage/memory_storage.cpp
)
target_compile_features(bengear_workflow_storage PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_storage PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_workflow_storage)

# ==================== bengear_workflow_scheduler ====================

add_library(bengear_workflow_scheduler STATIC
    src/workflow/scheduler.cpp
)
target_compile_features(bengear_workflow_scheduler PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_scheduler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_scheduler PUBLIC bengear_base bengear_json bengear_workflow_executor bengear_workflow_metrics)
bengear_apply_warnings(bengear_workflow_scheduler)

# ==================== bengear_workflow_visualizer ====================

add_library(bengear_workflow_visualizer STATIC
    src/workflow/visualizer.cpp
)
target_compile_features(bengear_workflow_visualizer PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_visualizer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_visualizer PUBLIC bengear_base bengear_json bengear_workflow_core)
bengear_apply_warnings(bengear_workflow_visualizer)

# ==================== bengear_workspace_uuid ====================

add_library(bengear_workspace_uuid STATIC
    src/workspace/uuid.cpp
)
target_compile_features(bengear_workspace_uuid PUBLIC cxx_std_20)
target_include_directories(bengear_workspace_uuid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace_uuid PUBLIC bengear_base)
bengear_apply_warnings(bengear_workspace_uuid)

# ==================== bengear_workspace_history ====================

add_library(bengear_workspace_history STATIC
    src/workspace/history_db.cpp
    src/workspace/history_exporter.cpp
)
target_compile_features(bengear_workspace_history PUBLIC cxx_std_20)
target_include_directories(bengear_workspace_history PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace_history PUBLIC bengear_base bengear_json sqlite3)
bengear_apply_warnings(bengear_workspace_history)

# ==================== bengear_workspace_manager ====================

add_library(bengear_workspace_manager STATIC
    src/workspace/manager.cpp
)
target_compile_features(bengear_workspace_manager PUBLIC cxx_std_20)
target_include_directories(bengear_workspace_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace_manager PUBLIC bengear_base bengear_json bengear_memory bengear_context)
bengear_apply_warnings(bengear_workspace_manager)

# ==================== bengear_workspace_session ====================

add_library(bengear_workspace_session STATIC
    src/workspace/conversation_history.cpp
    src/workspace/session.cpp
)
target_compile_features(bengear_workspace_session PUBLIC cxx_std_20)
target_include_directories(bengear_workspace_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace_session PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_llm_core
    bengear_memory
    bengear_context
    bengear_workflow_core
    bengear_workspace_uuid
    bengear_workspace_history
)
bengear_apply_warnings(bengear_workspace_session)

# ==================== bengear_workspace ====================

add_library(bengear_workspace INTERFACE)
target_compile_features(bengear_workspace INTERFACE cxx_std_20)
target_include_directories(bengear_workspace INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace INTERFACE
    bengear_workspace_uuid
    bengear_workspace_history
    bengear_workspace_manager
    bengear_workspace_session
)

# ==================== bengear_llm_core ====================

add_library(bengear_llm_core STATIC
    src/llm/adapter.cpp
    src/llm/cooldown_tracker.cpp
)
target_compile_features(bengear_llm_core PUBLIC cxx_std_20)
target_include_directories(bengear_llm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_llm_core PUBLIC bengear_base bengear_json bengear_acp_core)
bengear_apply_warnings(bengear_llm_core)

# ==================== bengear_application_workspace ====================

add_library(bengear_application_workspace STATIC
    src/application/workspace_resolver.cpp
)
target_compile_features(bengear_application_workspace PUBLIC cxx_std_20)
target_include_directories(bengear_application_workspace PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_application_workspace PUBLIC bengear_base bengear_json bengear_domain bengear_workspace)
bengear_apply_warnings(bengear_application_workspace)

# ==================== bengear_application_command ====================

add_library(bengear_application_command STATIC
    src/application/command_pipeline.cpp
    src/application/command_descriptor_factory.cpp
    src/application/command_governance.cpp
)
target_compile_features(bengear_application_command PUBLIC cxx_std_20)
target_include_directories(bengear_application_command PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_application_command PUBLIC bengear_base bengear_json bengear_domain)
bengear_apply_warnings(bengear_application_command)

# ==================== bengear_application_patch ====================

add_library(bengear_application_patch STATIC
    src/application/patch_use_cases.cpp
)
target_compile_features(bengear_application_patch PUBLIC cxx_std_20)
target_include_directories(bengear_application_patch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_application_patch PUBLIC bengear_base bengear_json bengear_domain bengear_patch bengear_application_workspace bengear_application_command)
bengear_apply_warnings(bengear_application_patch)

# ==================== bengear_application ====================

add_library(bengear_application INTERFACE)
target_compile_features(bengear_application INTERFACE cxx_std_20)
target_include_directories(bengear_application INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_application INTERFACE
    bengear_application_workspace
    bengear_application_command
    bengear_application_patch
)

# ==================== bengear_orchestration_plan ====================

add_library(bengear_orchestration_plan STATIC
    src/orchestration/plan.cpp
)
target_compile_features(bengear_orchestration_plan PUBLIC cxx_std_20)
target_include_directories(bengear_orchestration_plan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_orchestration_plan PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_orchestration_plan)

# ==================== bengear_orchestration_store ====================

add_library(bengear_orchestration_store STATIC
    src/orchestration/store.cpp
)
target_compile_features(bengear_orchestration_store PUBLIC cxx_std_20)
target_include_directories(bengear_orchestration_store PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_orchestration_store PUBLIC bengear_base bengear_json bengear_llm_core)
bengear_apply_warnings(bengear_orchestration_store)

# ==================== bengear_orchestration_todo ====================

add_library(bengear_orchestration_todo STATIC
    src/orchestration/todo.cpp
)
target_compile_features(bengear_orchestration_todo PUBLIC cxx_std_20)
target_include_directories(bengear_orchestration_todo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_orchestration_todo PUBLIC bengear_base bengear_json bengear_orchestration_plan)
bengear_apply_warnings(bengear_orchestration_todo)

# ==================== bengear_orchestration_serialization ====================

add_library(bengear_orchestration_serialization STATIC
    src/orchestration/plan_parser.cpp
    src/orchestration/serializer.cpp
)
target_compile_features(bengear_orchestration_serialization PUBLIC cxx_std_20)
target_include_directories(bengear_orchestration_serialization PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_orchestration_serialization PUBLIC bengear_base bengear_json bengear_orchestration_plan bengear_orchestration_store bengear_orchestration_todo)
bengear_apply_warnings(bengear_orchestration_serialization)

# ==================== bengear_orchestration ====================

add_library(bengear_orchestration INTERFACE)
target_compile_features(bengear_orchestration INTERFACE cxx_std_20)
target_include_directories(bengear_orchestration INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_orchestration INTERFACE
    bengear_orchestration_plan
    bengear_orchestration_store
    bengear_orchestration_todo
    bengear_orchestration_serialization
)

# ==================== bengear_workflow_core ====================

add_library(bengear_workflow_core STATIC
    src/workflow/workflow_engine.cpp
)
target_compile_features(bengear_workflow_core PUBLIC cxx_std_20)
target_include_directories(bengear_workflow_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow_core PUBLIC bengear_base bengear_json bengear_net bengear_tool bengear_domain bengear_workflow_tasks bengear_workflow_executor bengear_workflow_scheduler bengear_workflow_storage bengear_workflow_metrics bengear_workflow_approval)
bengear_apply_warnings(bengear_workflow_core)

# ==================== bengear_workspace_index ====================

add_library(bengear_workspace_index STATIC
    src/workspace_index/workspace_index_service.cpp
    src/workspace_index/request_index_session.cpp
)
target_compile_features(bengear_workspace_index PUBLIC cxx_std_20)
target_include_directories(bengear_workspace_index PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workspace_index PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_workspace_index)

# ==================== bengear_repo_map ====================

add_library(bengear_repo_map STATIC
    src/repo_map/repo_map_service.cpp
)
target_compile_features(bengear_repo_map PUBLIC cxx_std_20)
target_include_directories(bengear_repo_map PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_repo_map PUBLIC bengear_base bengear_json bengear_domain bengear_git bengear_test_loop bengear_workspace_index)
bengear_apply_warnings(bengear_repo_map)

# ==================== bengear_code_intel ====================

add_library(bengear_code_intel STATIC
    src/code_intel/code_intel_service.cpp
)
target_compile_features(bengear_code_intel PUBLIC cxx_std_20)
target_include_directories(bengear_code_intel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_code_intel PUBLIC bengear_base bengear_json bengear_domain bengear_repo_map bengear_workspace_index)
bengear_apply_warnings(bengear_code_intel)

# ==================== bengear_diagnostic_context ====================

add_library(bengear_diagnostic_context STATIC
    src/diagnostic_context/diagnostic_context_service.cpp
)
target_compile_features(bengear_diagnostic_context PUBLIC cxx_std_20)
target_include_directories(bengear_diagnostic_context PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_diagnostic_context PUBLIC bengear_base bengear_json bengear_domain bengear_test_loop bengear_code_intel bengear_workspace_index)
bengear_apply_warnings(bengear_diagnostic_context)

# ==================== bengear_diagnostic_repair_plan ====================

add_library(bengear_diagnostic_repair_plan STATIC
    src/diagnostic_repair/diagnostic_repair_plan_service.cpp
)
target_compile_features(bengear_diagnostic_repair_plan PUBLIC cxx_std_20)
target_include_directories(bengear_diagnostic_repair_plan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_diagnostic_repair_plan PUBLIC bengear_base bengear_json bengear_domain bengear_diagnostic_context bengear_workspace_index)
bengear_apply_warnings(bengear_diagnostic_repair_plan)

# ==================== bengear_diagnostic_repair_patch_preview ====================

add_library(bengear_diagnostic_repair_patch_preview STATIC
    src/diagnostic_repair/diagnostic_repair_patch_preview_service.cpp
)
target_compile_features(bengear_diagnostic_repair_patch_preview PUBLIC cxx_std_20)
target_include_directories(bengear_diagnostic_repair_patch_preview PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_diagnostic_repair_patch_preview PUBLIC bengear_base bengear_json bengear_domain bengear_diagnostic_repair_plan bengear_patch)
bengear_apply_warnings(bengear_diagnostic_repair_patch_preview)

# ==================== bengear_diagnostic ====================

add_library(bengear_diagnostic INTERFACE)
target_compile_features(bengear_diagnostic INTERFACE cxx_std_20)
target_include_directories(bengear_diagnostic INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_diagnostic INTERFACE
    bengear_diagnostic_context
    bengear_diagnostic_repair_plan
    bengear_diagnostic_repair_patch_preview
)

# ==================== bengear_permission ====================

add_library(bengear_permission STATIC
    src/permission/policy_engine.cpp
)
target_compile_features(bengear_permission PUBLIC cxx_std_20)
target_include_directories(bengear_permission PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_permission PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_permission)

# ==================== bengear_audit ====================

add_library(bengear_audit STATIC
    src/audit/audit_store.cpp
)
target_compile_features(bengear_audit PUBLIC cxx_std_20)
target_include_directories(bengear_audit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_audit PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_audit)

# ==================== bengear_git ====================

add_library(bengear_git STATIC
    src/git/git_service.cpp
)
target_compile_features(bengear_git PUBLIC cxx_std_20)
target_include_directories(bengear_git PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_git PUBLIC bengear_base bengear_json bengear_domain)
bengear_apply_warnings(bengear_git)

# ==================== bengear_checkpoint ====================

add_library(bengear_checkpoint STATIC
    src/checkpoint/checkpoint_service.cpp
)
target_compile_features(bengear_checkpoint PUBLIC cxx_std_20)
target_include_directories(bengear_checkpoint PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_checkpoint PUBLIC bengear_base bengear_json bengear_domain)
bengear_apply_warnings(bengear_checkpoint)

# ==================== bengear_test_loop_diagnostics ====================

add_library(bengear_test_loop_diagnostics STATIC
    src/test_loop/diagnostics.cpp
)
target_compile_features(bengear_test_loop_diagnostics PUBLIC cxx_std_20)
target_include_directories(bengear_test_loop_diagnostics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_test_loop_diagnostics PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_test_loop_diagnostics)

# ==================== bengear_test_loop_service ====================

add_library(bengear_test_loop_service STATIC
    src/test_loop/test_loop_service.cpp
)
target_compile_features(bengear_test_loop_service PUBLIC cxx_std_20)
target_include_directories(bengear_test_loop_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_test_loop_service PUBLIC bengear_base bengear_json bengear_domain bengear_test_loop_diagnostics)
bengear_apply_warnings(bengear_test_loop_service)

# ==================== bengear_test_loop ====================

add_library(bengear_test_loop INTERFACE)
target_compile_features(bengear_test_loop INTERFACE cxx_std_20)
target_include_directories(bengear_test_loop INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_test_loop INTERFACE
    bengear_test_loop_diagnostics
    bengear_test_loop_service
)

# ==================== bengear_patch_diff ====================

add_library(bengear_patch_diff STATIC
    src/patch/diff_parser.cpp
)
target_compile_features(bengear_patch_diff PUBLIC cxx_std_20)
target_include_directories(bengear_patch_diff PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_patch_diff PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_patch_diff)

# ==================== bengear_patch_store ====================

add_library(bengear_patch_store STATIC
    src/patch/change_store.cpp
)
target_compile_features(bengear_patch_store PUBLIC cxx_std_20)
target_include_directories(bengear_patch_store PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_patch_store PUBLIC bengear_base bengear_json bengear_workspace)
bengear_apply_warnings(bengear_patch_store)

# ==================== bengear_patch_service ====================

add_library(bengear_patch_service STATIC
    src/patch/patch_service.cpp
)
target_compile_features(bengear_patch_service PUBLIC cxx_std_20)
target_include_directories(bengear_patch_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_patch_service PUBLIC bengear_base bengear_json bengear_domain bengear_workspace bengear_patch_diff bengear_patch_store)
bengear_apply_warnings(bengear_patch_service)

# ==================== bengear_patch ====================

add_library(bengear_patch INTERFACE)
target_compile_features(bengear_patch INTERFACE cxx_std_20)
target_include_directories(bengear_patch INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_patch INTERFACE
    bengear_patch_diff
    bengear_patch_store
    bengear_patch_service
)

# ==================== bengear_memory_section_merge ====================

add_library(bengear_memory_section_merge STATIC
    src/memory/section_merge.cpp
)
target_compile_features(bengear_memory_section_merge PUBLIC cxx_std_20)
target_include_directories(bengear_memory_section_merge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_memory_section_merge PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_memory_section_merge)

# ==================== bengear_memory_store ====================

add_library(bengear_memory_store STATIC
    src/memory/store.cpp
)
target_compile_features(bengear_memory_store PUBLIC cxx_std_20)
target_include_directories(bengear_memory_store PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_memory_store PUBLIC bengear_base bengear_json bengear_memory_section_merge)
bengear_apply_warnings(bengear_memory_store)

# ==================== bengear_memory_episode ====================

add_library(bengear_memory_episode STATIC
    src/memory/episode.cpp
)
target_compile_features(bengear_memory_episode PUBLIC cxx_std_20)
target_include_directories(bengear_memory_episode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_memory_episode PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_memory_episode)

# ==================== bengear_memory_updater ====================

add_library(bengear_memory_updater STATIC
    src/memory/updater.cpp
)
target_compile_features(bengear_memory_updater PUBLIC cxx_std_20)
target_include_directories(bengear_memory_updater PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_memory_updater PUBLIC bengear_base bengear_json bengear_memory_store bengear_memory_episode)
bengear_apply_warnings(bengear_memory_updater)

# ==================== bengear_memory ====================

add_library(bengear_memory INTERFACE)
target_compile_features(bengear_memory INTERFACE cxx_std_20)
target_include_directories(bengear_memory INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_memory INTERFACE
    bengear_memory_section_merge
    bengear_memory_store
    bengear_memory_episode
    bengear_memory_updater
)

# ==================== bengear_skill ====================

add_library(bengear_skill STATIC
    src/skill/skill.cpp
    src/skill/zip_extract.cpp
)
target_compile_features(bengear_skill PUBLIC cxx_std_20)
target_include_directories(bengear_skill PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress)
bengear_apply_warnings(bengear_skill)

# ==================== bengear_context_core ====================

add_library(bengear_context_core STATIC
    src/memory/context.cpp
)
target_compile_features(bengear_context_core PUBLIC cxx_std_20)
target_include_directories(bengear_context_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_context_core PUBLIC bengear_base bengear_json bengear_memory bengear_workspace)
bengear_apply_warnings(bengear_context_core)

# ==================== bengear_context_pruner ====================

add_library(bengear_context_pruner STATIC
    src/memory/context_pruner.cpp
)
target_compile_features(bengear_context_pruner PUBLIC cxx_std_20)
target_include_directories(bengear_context_pruner PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_context_pruner PUBLIC bengear_base bengear_json bengear_acp_core)
bengear_apply_warnings(bengear_context_pruner)

# ==================== bengear_context_compactor ====================

add_library(bengear_context_compactor STATIC
    src/memory/compactor.cpp
)
target_compile_features(bengear_context_compactor PUBLIC cxx_std_20)
target_include_directories(bengear_context_compactor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_context_compactor PUBLIC bengear_base bengear_json bengear_memory bengear_context_core bengear_workspace)
bengear_apply_warnings(bengear_context_compactor)

# ==================== bengear_context ====================

add_library(bengear_context INTERFACE)
target_compile_features(bengear_context INTERFACE cxx_std_20)
target_include_directories(bengear_context INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_context INTERFACE
    bengear_context_core
    bengear_context_pruner
    bengear_context_compactor
)

# ==================== bengear_workflow ====================

add_library(bengear_workflow INTERFACE)
target_compile_features(bengear_workflow INTERFACE cxx_std_20)
target_include_directories(bengear_workflow INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow INTERFACE
    bengear_workflow_tasks
    bengear_workflow_executor
    bengear_workflow_metrics
    bengear_workflow_approval
    bengear_workflow_storage
    bengear_workflow_scheduler
    bengear_workflow_core
    bengear_workflow_visualizer
)

# ==================== bengear_acp_core ====================

add_library(bengear_acp_core STATIC
    src/acp/core/content_block.cpp
    src/acp/core/message.cpp
)

target_compile_features(bengear_acp_core PUBLIC cxx_std_20)
target_include_directories(bengear_acp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_acp_core PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_acp_core)

# ==================== bengear_acp_stream ====================

add_library(bengear_acp_stream INTERFACE)
target_compile_features(bengear_acp_stream INTERFACE cxx_std_20)
target_include_directories(bengear_acp_stream INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_acp_stream INTERFACE bengear_acp_core)

# ==================== bengear_acp_adapter ====================

add_library(bengear_acp_adapter INTERFACE)
target_compile_features(bengear_acp_adapter INTERFACE cxx_std_20)
target_include_directories(bengear_acp_adapter INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_acp_adapter INTERFACE bengear_acp_core bengear_acp_stream bengear_tool)

# ==================== bengear_cli_render_core ====================

add_library(bengear_cli_render_core STATIC
    src/cli/render/renderer.cpp
)
target_compile_features(bengear_cli_render_core PUBLIC cxx_std_20)
target_include_directories(bengear_cli_render_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_render_core PUBLIC bengear_base bengear_json bengear_agent_runtime)
bengear_apply_warnings(bengear_cli_render_core)

# ==================== bengear_cli_repl_core ====================

add_library(bengear_cli_repl_core STATIC
    src/cli/repl/history_store.cpp
    src/cli/repl/line_editor.cpp
    src/cli/repl/terminal_io.cpp
)
target_compile_features(bengear_cli_repl_core PUBLIC cxx_std_20)
target_include_directories(bengear_cli_repl_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_repl_core PUBLIC bengear_base bengear_json)
bengear_apply_warnings(bengear_cli_repl_core)

# ==================== bengear_cli_runtime ====================

add_library(bengear_cli_runtime INTERFACE)
target_compile_features(bengear_cli_runtime INTERFACE cxx_std_20)
target_include_directories(bengear_cli_runtime INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_runtime INTERFACE
    bengear_agent_runtime
    bengear_server
    bengear_workflow_core
    bengear_workflow_visualizer
)

# ==================== CLI ====================

add_library(bengear_cli_render STATIC
    src/cli/render/cli_app.cpp
)
target_compile_features(bengear_cli_render PUBLIC cxx_std_20)
target_include_directories(bengear_cli_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_render PUBLIC bengear_cli_render_core bengear_cli_runtime)

add_library(bengear_cli_repl STATIC
    src/cli/repl/chat_repl.cpp
)
target_compile_features(bengear_cli_repl PUBLIC cxx_std_20)
target_include_directories(bengear_cli_repl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_repl PUBLIC bengear_cli_render bengear_cli_repl_core bengear_cli_runtime)

add_executable(bengear src/main.cpp)
target_link_libraries(bengear PRIVATE bengear_cli_repl)

# ==================== 警告 ====================

bengear_apply_warnings(bengear_base)
bengear_apply_warnings(bengear_net)
bengear_apply_warnings(bengear_tool)
bengear_apply_warnings(bengear)
bengear_apply_warnings(bengear_cli_render)
bengear_apply_warnings(bengear_cli_repl)

# macOS: 自动生成 dSYM 调试符号（崩溃时显示行号）
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET bengear POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:bengear> -o $<TARGET_FILE:bengear>.dSYM
        COMMENT "Generating dSYM debug symbols for crash backtraces"
    )
endif()

# ==================== bengear_test_runtime ====================

add_library(bengear_test_runtime INTERFACE)
target_compile_features(bengear_test_runtime INTERFACE cxx_std_20)
target_include_directories(bengear_test_runtime INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_test_runtime INTERFACE
    bengear_cli_runtime
    bengear_acp_core
    bengear_acp_stream
    bengear_acp_adapter
    bengear_workflow
)

# ==================== Tests ====================

if(BEN_GEAR_BUILD_TESTS)
    enable_testing()

    set(BENGEAR_TEST_SOURCES
        tests/test_main.cpp
        tests/test_base_utils.cpp
        tests/test_domain_event.cpp
        tests/test_llm_clients.cpp
        tests/test_llm_stream.cpp
        tests/test_llm_retry.cpp
        tests/test_llm_endpoint.cpp
        tests/test_config.cpp
        tests/test_net.cpp
        tests/test_session.cpp
        tests/test_memory.cpp
        tests/test_memory_episode.cpp
        tests/test_workspace.cpp
        tests/test_tool.cpp
        tests/test_pool.cpp
        tests/test_container.cpp
        tests/test_file_lock.cpp
        tests/test_orchestration.cpp
        tests/test_workflow.cpp
        tests/test_workflow_extended.cpp
        tests/test_agent.cpp
        tests/test_lifecycle.cpp
        tests/test_json.cpp
        tests/test_acp.cpp
        tests/test_failover.cpp
        tests/test_context_pruner.cpp
        tests/test_usage.cpp
        tests/test_context_overflow.cpp
        tests/test_plan_mode.cpp
        tests/test_sub_agent.cpp
        tests/test_history_db.cpp
        tests/test_server.cpp
        tests/test_application_architecture.cpp
        tests/test_command_governance.cpp
        tests/test_patch.cpp
        tests/test_git_tools.cpp
        tests/test_checkpoint.cpp
        tests/test_test_loop.cpp
        tests/test_diagnostic_context.cpp
        tests/test_diagnostic_repair_plan.cpp
        tests/test_diagnostic_repair_patch_preview.cpp
        tests/test_permission.cpp
        tests/test_repo_map.cpp
        tests/test_workspace_index.cpp
        tests/test_code_intel.cpp
        tests/test_audit_store.cpp
    )

    add_executable(bengear_tests ${BENGEAR_TEST_SOURCES})
    target_link_libraries(bengear_tests PRIVATE
        bengear_test_runtime
    )
    add_test(NAME bengear_tests COMMAND bengear_tests)
endif()

# ==================== Examples ====================

if(BEN_GEAR_BUILD_EXAMPLES)
    add_executable(example_config_layers examples/config_layers.cpp)
    target_link_libraries(example_config_layers PRIVATE bengear_net)
    bengear_apply_warnings(example_config_layers)

    add_executable(example_json_support examples/json_support.cpp)
    target_link_libraries(example_json_support PRIVATE bengear_net)
    bengear_apply_warnings(example_json_support)

    add_executable(example_tcp_fetch examples/tcp_fetch.cpp)
    target_link_libraries(example_tcp_fetch PRIVATE bengear_net)
    bengear_apply_warnings(example_tcp_fetch)

    add_executable(example_agent_cli examples/agent_cli.cpp)
    target_link_libraries(example_agent_cli PRIVATE bengear_workflow)
    bengear_apply_warnings(example_agent_cli)

    add_executable(example_base_components examples/base_components_example.cpp)
    target_link_libraries(example_base_components PRIVATE bengear_base)
    bengear_apply_warnings(example_base_components)

    add_executable(example_workflow_weather examples/workflow_weather_example.cpp)
    target_link_libraries(example_workflow_weather PRIVATE bengear_workflow)
    bengear_apply_warnings(example_workflow_weather)

    add_executable(example_acp examples/acp_example.cpp)
    target_link_libraries(example_acp PRIVATE bengear_acp_core bengear_workflow)
    bengear_apply_warnings(example_acp)
endif()

# ==================== Benchmarks ====================

if(BEN_GEAR_BUILD_BENCHMARKS)
    add_executable(bengear_benchmarks benchmarks/benchmark_main.cpp)
    target_link_libraries(bengear_benchmarks PRIVATE bengear_net)
    bengear_apply_warnings(bengear_benchmarks)

    add_executable(performance_benchmark benchmarks/performance_benchmark.cpp)
    target_link_libraries(performance_benchmark PRIVATE bengear_base)
    bengear_apply_warnings(performance_benchmark)

    add_executable(memory_pool_stress benchmarks/memory_pool_stress.cpp)
    target_link_libraries(memory_pool_stress PRIVATE bengear_base)
    bengear_apply_warnings(memory_pool_stress)

    add_executable(json_benchmark benchmarks/json_benchmark.cpp)
    target_link_libraries(json_benchmark PRIVATE bengear_json bengear_base sqlite3)
    bengear_apply_warnings(json_benchmark)

    add_executable(workflow_benchmark benchmarks/workflow_benchmark.cpp)
    target_link_libraries(workflow_benchmark PRIVATE bengear_workflow)
    bengear_apply_warnings(workflow_benchmark)

    add_executable(eventloop_benchmark benchmarks/eventloop_benchmark.cpp)
    target_link_libraries(eventloop_benchmark PRIVATE bengear_workflow)
    bengear_apply_warnings(eventloop_benchmark)
endif()

install(TARGETS bengear RUNTIME DESTINATION bin)
# 版本号宏定义（PRIVATE：仅目标内部使用，不传递给依赖方）
target_compile_definitions(bengear_cli_repl PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
