#!/bin/bash
# macOS core dump 配置脚本
# 使用方法: source scripts/enable_core.sh

# 1. 确认系统允许 core dump
echo "当前 kern.coredump = $(sysctl -n kern.coredump)"
echo "当前 kern.corefile = $(sysctl -n kern.corefile)"

# 2. 设置 ulimit（仅当前 shell 生效）
ulimit -c unlimited
echo "ulimit -c = $(ulimit -c)"

# 3. 移除代码签名（macOS 会阻止已签名程序生成 core）
if [ -f ./build-dbg/bengear ]; then
    codesign --remove-signature ./build-dbg/bengear 2>/dev/null && echo "已移除 build-dbg/bengear 签名" || echo "无需移除签名"
fi
if [ -f ./build/bengear ]; then
    codesign --remove-signature ./build/bengear 2>/dev/null && echo "已移除 build/bengear 签名" || echo "无需移除签名"
fi

# 4. 验证
echo "Core 文件路径: $(sysctl -n kern.corefile)"
echo ""
echo "配置完成！崩溃后分析:"
echo "  lldb ./build-dbg/bengear /cores/core.<PID>"
echo "  (lldb) bt 30"
