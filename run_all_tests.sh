#!/usr/bin/env bash
# run_all_tests.sh — 跨平台运行所有 BenGear 单测
#
# 用法:
#   ./run_all_tests.sh                   # 运行全部（单测 60s 超时）
#   ./run_all_tests.sh --verbose         # 详细输出
#   ./run_all_tests.sh --timeout=120     # 自定义超时秒数
#   ./run_all_tests.sh --xml=result.xml  # 输出 JUnit XML
#
# 跨平台: Linux / macOS / Windows (Git Bash / WSL)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TESTS_DIR="${BUILD_DIR}/tests"

VERBOSE=""
XML_OUTPUT=""
XML_TMP_DIR=""
TIMEOUT=60  # 单个测试超时秒数

# 解析参数
for arg in "$@"; do
    case "$arg" in
        --verbose|-v) VERBOSE="--verbose" ;;
        --xml=*)      XML_OUTPUT="${arg#--xml=}" ;;
        --xml)        shift; XML_OUTPUT="$1" ;;
        --timeout=*)  TIMEOUT="${arg#--timeout=}" ;;
        --timeout)    shift; TIMEOUT="$1" ;;
        *)            echo "未知参数: $arg"; exit 1 ;;
    esac
    shift 2>/dev/null || true
done

# 检测测试可执行文件
EXE_SUFFIX=""
if [[ "$(uname -s)" == *"MINGW"* ]] || [[ "$(uname -s)" == *"MSYS"* ]]; then
    EXE_SUFFIX=".exe"
fi

shopt -s nullglob
TEST_EXES=("$TESTS_DIR"/test_*"$EXE_SUFFIX")
shopt -u nullglob

if [ ${#TEST_EXES[@]} -eq 0 ]; then
    echo "错误: 未找到测试可执行文件 ($TESTS_DIR/test_*$EXE_SUFFIX)"
    echo "请先构建: cmake --build $BUILD_DIR --target bengear_tests_build"
    exit 1
fi

# XML 分片目录
if [ -n "$XML_OUTPUT" ]; then
    XML_TMP_DIR="${BUILD_DIR}/test_results"
    mkdir -p "$XML_TMP_DIR"
fi

TOTAL=0
PASS=0
FAIL=0

echo "========================================="
echo "  BenGear 单元测试"
echo "  共 ${#TEST_EXES[@]} 个测试目标"
echo "========================================="
echo ""

for exe in "${TEST_EXES[@]}"; do
    name="$(basename "$exe" "$EXE_SUFFIX")"
    TOTAL=$((TOTAL + 1))
    printf "[%d/%d] %-35s " "$TOTAL" "${#TEST_EXES[@]}" "$name"

    ARGS=("$exe")
    [ -n "$VERBOSE" ] && ARGS+=("$VERBOSE")

    if [ -n "$XML_OUTPUT" ]; then
        PER_TEST_XML="${XML_TMP_DIR}/${name}.xml"
        ARGS+=("--xml=$PER_TEST_XML")
    fi

    # 执行测试（带超时保护，输出写入临时文件避免管道缓冲区死锁）
    TMP_LOG=$(mktemp)
    timeout "$TIMEOUT" "${ARGS[@]}" >"$TMP_LOG" 2>&1
    EXIT_CODE=$?
    if [ "$EXIT_CODE" -eq 0 ]; then
        PASS=$((PASS + 1))
        echo "PASS"
    else
        FAIL=$((FAIL + 1))
        if [ "$EXIT_CODE" -eq 124 ]; then
            echo "TIMEOUT (${TIMEOUT}s)"
            echo "(测试超时 ${TIMEOUT}s)" >> "$TMP_LOG"
        else
            echo "FAIL (exit=$EXIT_CODE)"
        fi
    fi

    # 输出测试日志
    if [ -s "$TMP_LOG" ]; then
        echo "  ─────────────────────────────────────────"
        sed 's/^/  | /' "$TMP_LOG"
        echo "  ─────────────────────────────────────────"
    fi
    rm -f "$TMP_LOG"

    echo ""
done

# 合并 XML 分片
if [ -n "$XML_OUTPUT" ]; then
    {
        echo '<?xml version="1.0" encoding="UTF-8"?>'
        echo '<testsuites>'
        for f in "$XML_TMP_DIR"/*.xml; do
            [ -f "$f" ] || continue
            # 提取 <testsuite>...</testsuite>
            sed -n '/<testsuite/,/<\/testsuite>/p' "$f"
        done
        echo '</testsuites>'
    } > "$XML_OUTPUT"
    echo "JUnit XML 已输出: $XML_OUTPUT"
    echo ""
fi

echo "========================================="
echo "  结果: $PASS/$TOTAL 通过"
echo "========================================="

if [ "$FAIL" -gt 0 ]; then
    echo "  $FAIL 个测试失败"
    exit 1
else
    echo "  全部通过!"
    exit 0
fi
