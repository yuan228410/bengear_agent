# BenGear run_tests.cmake — 不依赖 CTest，纯 CMake 脚本执行所有测试
#
# 用法：
#   cmake --build build --target run_tests
#
# 等价：
#   cmake -DBENGEAR_TEST_TARGETS="test_base;test_net;..." \
#         -DBINARY_DIR=build -P tests/run_tests.cmake
#
# 选项：--verbose  -v      详细输出
#       --xml=<file>        合并的 JUnit XML 输出路径

cmake_minimum_required(VERSION 3.15)

set(verbose OFF)
set(xml_output "")

foreach(_arg ${CMAKE_ARGV})
    if(_arg STREQUAL "--verbose" OR _arg STREQUAL "-v")
        set(verbose ON)
    elseif(_arg MATCHES "^--xml=(.+)$")
        set(xml_output "${CMAKE_MATCH_1}")
    endif()
endforeach()

# 收集测试列表
if(DEFINED BENGEAR_TEST_TARGETS AND NOT BENGEAR_TEST_TARGETS STREQUAL "")
    separate_arguments(_targets NATIVE_COMMAND "${BENGEAR_TEST_TARGETS}")
else()
    message(FATAL_ERROR "BENGEAR_TEST_TARGETS is empty. Did you build with -DBEN_GEAR_BUILD_TESTS=ON?")
endif()

# XML 分片目录 (避免覆盖)
set(_xml_tmp_dir "")
if(xml_output)
    set(_xml_tmp_dir "${BINARY_DIR}/test_results")
    file(MAKE_DIRECTORY "${_xml_tmp_dir}")
endif()

set(total_pass 0)
set(total_fail 0)
set(total 0)

foreach(_target IN LISTS _targets)
    set(_exe "${BINARY_DIR}/tests/${_target}${EXE_SUFFIX}")

    if(NOT EXISTS "${_exe}")
        message(WARNING "Test executable not found: ${_exe}")
        continue()
    endif()

    math(EXPR total "${total} + 1")

    # 每个测试写独立的 XML 分片
    set(_args "${_exe}")
    if(verbose)
        list(APPEND _args "--verbose")
    endif()
    if(xml_output)
        set(_per_test_xml "${_xml_tmp_dir}/${_target}.xml")
        list(APPEND _args "--xml=${_per_test_xml}")
    endif()

    message(STATUS "[${total}] ${_target}")
    execute_process(
        COMMAND ${_args}
        RESULT_VARIABLE _exit_code
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )

    if(_stdout)
        message("${_stdout}")
    endif()
    if(_stderr)
        message("${_stderr}")
    endif()

    if(_exit_code EQUAL 0)
        math(EXPR total_pass "${total_pass} + 1")
        message(STATUS "  ✅ ${_target}")
    else()
        math(EXPR total_fail "${total_fail} + 1")
        message(STATUS "  ❌ ${_target} (exit ${_exit_code})")
    endif()
endforeach()

# 合并 XML 分片 → 单一 JUnit XML
if(xml_output)
    file(WRITE "${xml_output}"
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<testsuites>\n")
    set(_has_tests FALSE)
    file(GLOB _xml_files "${_xml_tmp_dir}/*.xml")
    foreach(_f ${_xml_files})
        file(READ "${_f}" _content)
        # 提取 <testsuite ...> ... </testsuite>，去掉 XML 声明和 <testsuites> 包装
        if(_content MATCHES "<testsuite[^>]*>.*</testsuite>")
            file(APPEND "${xml_output}" "${CMAKE_MATCH_0}\n")
            set(_has_tests TRUE)
        endif()
    endforeach()
    file(APPEND "${xml_output}" "</testsuites>\n")
    if(_has_tests)
        message(STATUS "Merged JUnit XML → ${xml_output}")
    endif()
endif()

# 汇总
message(STATUS "")
message(STATUS "=================================================")
message(STATUS "  Results: ${total_pass}/${total} passed")
if(total_fail GREATER 0)
    message(FATAL_ERROR "  ${total_fail} test(s) failed")
else()
    message(STATUS "  All tests passed!")
endif()
