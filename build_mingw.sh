#!/bin/bash
# BenGear 编译脚本 (MinGW/w64devkit on Windows)
# 用法: ./build_mingw.sh [Debug|RelWithDebInfo|Release]
# 默认 RelWithDebInfo
set -e

BUILD_TYPE="${1:-RelWithDebInfo}"
BUILD_DIR="build"
NPROC=$(nproc 2>/dev/null || echo 20)

echo "=== Configure ($BUILD_TYPE) ==="
cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE=mingw_toolchain.cmake \
  -DBEN_GEAR_BUILD_EXAMPLES=OFF \
  -DBEN_GEAR_BUILD_BENCHMARKS=OFF

# 后处理：修复深信服拦截 .obj 的问题
# CMake Ninja generator 在 Windows 上强制 .obj，
# 但 GCC/w64devkit 会根据扩展名输出不同格式（.obj 被深信服拦截加密）
echo "=== Patching: .obj → .o ==="
sed -i 's/\.cpp\.obj\b/.cpp.o/g; s/\.c\.obj\b/.c.o/g' "$BUILD_DIR/build.ninja"
sed -i 's/\.cpp\.obj\.d\b/.cpp.o.d/g; s/\.c\.obj\.d\b/.c.o.d/g' "$BUILD_DIR/build.ninja"

echo "=== Patching: gcc-ar qc → gcc-ar crs ==="
sed -i 's/gcc-ar qc/gcc-ar crs/g; s/&& gcc-ranlib $TARGET_FILE //g' "$BUILD_DIR/CMakeFiles/rules.ninja"

echo "=== Build (-j$NPROC) ==="
cmake --build "$BUILD_DIR" -j"$NPROC"

echo ""
echo "=== Build complete! ==="
echo "Binary: $BUILD_DIR/bengear.exe"
echo ""
echo "Run tests: cmake --build $BUILD_DIR --target run_tests -j$NPROC"
