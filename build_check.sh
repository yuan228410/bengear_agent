#!/bin/bash
# BenGear 编译检查脚本
# 编译 bengear_base、performance_benchmark、memory_pool_stress 并运行

set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

cd "$BUILD_DIR"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "=== Step 1: CMake Configure ==="
cmake "$ROOT_DIR" -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3

echo ""
echo "=== Step 2: Build memory_pool_stress + performance_benchmark ==="
make memory_pool_stress performance_benchmark -j"$NPROC" 2>&1

echo ""
echo "=== Step 3: Run memory_pool_stress ==="
./memory_pool_stress 2>&1

echo ""
echo "=== Step 4: Run performance_benchmark ==="
./performance_benchmark 2>&1

echo ""
echo "=== ALL DONE ==="
