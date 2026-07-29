# MinGW toolchain for w64devkit on Windows
# 解决两个问题：
# 1. 深信服安全软件拦截 .obj 文件 → 强制 .o 扩展名（通过后处理脚本）
# 2. ar 不生成索引 → 使用 gcc-ar + crs

set(CMAKE_AR gcc-ar CACHE FILEPATH "gcc-ar can handle GCC object formats")
set(CMAKE_RANLIB gcc-ranlib CACHE FILEPATH "")
