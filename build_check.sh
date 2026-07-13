#!/bin/bash
# BenGear 编译检查脚本
# 编译所有测试目标并运行

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build-check"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "=== Step 1: CMake Configure ==="
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBEN_GEAR_BUILD_TESTS=ON -DBEN_GEAR_BUILD_EXAMPLES=OFF -DBEN_GEAR_BUILD_BENCHMARKS=OFF 2>&1 | tail -5

echo ""
echo "=== Step 2: Build all targets ==="
cmake --build "$BUILD_DIR" -j"$NPROC" 2>&1

echo ""
echo "=== Step 3: Run all tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure -j"$NPROC" 2>&1

echo ""
echo "=== ALL DONE ==="
