#!/bin/bash

# 编译并显示所有错误
cmake --build build -j4 2>&1 | grep "error:" | sort -u
