cmake_minimum_required(VERSION 3.20)
project(ben_gear VERSION 0.4.0 LANGUAGES C CXX)

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

# ==================== bengear_workflow ====================

add_library(bengear_workflow STATIC
    src/workflow/workflow_engine.cpp
    src/workflow/task_types.cpp
    src/workflow/executor.cpp
    src/workflow/scheduler.cpp
    src/workflow/storage/memory_storage.cpp
    src/workflow/metrics.cpp
    src/workflow/approval_manager.cpp
    src/workflow/visualizer.cpp
    src/workspace/history_db.cpp
    src/workspace/history_exporter.cpp
    src/workspace/conversation_history.cpp
    src/workspace/manager.cpp
    src/workspace/session.cpp
    src/workspace/uuid.cpp
    src/llm/adapter.cpp
    src/llm/cooldown_tracker.cpp

    src/agent/agent.cpp
    src/agent/shared_resources.cpp
    src/memory/compactor.cpp
    src/memory/context.cpp
    src/memory/store.cpp
    src/memory/context_pruner.cpp
    src/memory/updater.cpp
    src/memory/episode.cpp
    src/memory/section_merge.cpp
    src/skill/skill.cpp
    src/skill/zip_extract.cpp
    src/tool/types.cpp
    src/tool/registry.cpp
    src/tool/manager.cpp
    src/mcp/mcp_client.cpp
)

target_compile_features(bengear_workflow PUBLIC cxx_std_20)
target_include_directories(bengear_workflow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow PUBLIC bengear_net bengear_base bengear_json)

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
target_link_libraries(bengear_acp_adapter INTERFACE bengear_acp_core bengear_acp_stream bengear_workflow)

# ==================== CLI ====================

add_library(bengear_cli_render STATIC
    src/cli/render/renderer.cpp
    src/cli/render/cli_app.cpp
)
target_compile_features(bengear_cli_render PUBLIC cxx_std_20)
target_include_directories(bengear_cli_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_render PUBLIC bengear_workflow)

add_library(bengear_cli_repl STATIC
    src/cli/repl/chat_repl.cpp
    src/cli/repl/history_store.cpp
    src/cli/repl/line_editor.cpp
    src/cli/repl/terminal_io.cpp
)
target_compile_features(bengear_cli_repl PUBLIC cxx_std_20)
target_include_directories(bengear_cli_repl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_cli_repl PUBLIC bengear_cli_render bengear_workflow)

add_executable(bengear src/main.cpp)
target_link_libraries(bengear PRIVATE bengear_cli_repl)

# ==================== 警告 ====================

bengear_apply_warnings(bengear_base)
bengear_apply_warnings(bengear_net)
bengear_apply_warnings(bengear_workflow)
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

# ==================== Tests ====================

if(BEN_GEAR_BUILD_TESTS)
    enable_testing()

    set(BENGEAR_TEST_SOURCES
        tests/test_main.cpp
        tests/test_base_utils.cpp
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
        tests/test_workflow.cpp
        tests/test_agent.cpp
        tests/test_json.cpp
        tests/test_acp.cpp
        tests/test_failover.cpp
        tests/test_context_pruner.cpp
        tests/test_usage.cpp
        tests/test_context_overflow.cpp
        tests/test_plan_mode.cpp
        tests/test_history_db.cpp
    )

    add_executable(bengear_tests ${BENGEAR_TEST_SOURCES})
    target_link_libraries(bengear_tests PRIVATE
        bengear_workflow
        bengear_acp_core
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
target_compile_definitions(bengear_workflow PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
