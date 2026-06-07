# 快速开始

## 系统要求

- CMake 3.20+
- C++20 编译器
- macOS、Linux 或 Windows
- OpenSSL（用于 HTTPS）
- zlib（用于压缩）

原生 HTTP/HTTPS 请求直接使用 BenGear 的 socket 传输层，无需 curl 等外部工具。

## 构建

```bash
cmake -S . -B build
# 默认构建（RelWithDebInfo，带调试符号）
cmake --build build

# Debug 构建用于开发调试
cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg
```

可选 CMake 标志：

```bash
cmake -S . -B build \
  -DBEN_GEAR_BUILD_TESTS=ON \
  -DBEN_GEAR_BUILD_EXAMPLES=ON \
  -DBEN_GEAR_BUILD_BENCHMARKS=ON
```

## 测试

```bash
ctest --test-dir build --output-on-failure
# 或直接运行
./build/bengear_tests
# 过滤特定测试
./build/bengear_tests --gtest_filter=MemoryStoreTest.*
```

## 运行

### 交互式聊天

```bash
./build/bengear
```

进入 REPL 模式后支持完整的行编辑功能：

| 功能 | 操作 |
|------|------|
| 行编辑 | ← → Home End Ctrl+A/E Ctrl+U/K/W |
| 历史浏览 | ↑ ↓（自动保存到 `~/.bengear/history`） |
| 命令补全 | 输入 `/` 自动显示候选，Tab 切换 |
| 退出 | 连按两次 Ctrl+C，或输入 `/exit` |

内置 `/` 命令：`/help` `/exit` `/new` `/sessions` `/resume <id>` `/compact` `/clear` `/model`

### 单次提示

```bash
./build/bengear "你好，介绍一下 BenGear"
```

### 从 stdin 读取提示

```bash
cat prompt.txt | ./build/bengear --stdin
```

### 显示配置

```bash
./build/bengear --show-config
```

### 运行时覆盖模型

```bash
./build/bengear --active-model oneapi-claude-sonnet "hello"
```

### 工作空间管理

```bash
./build/bengear workspace list
./build/bengear workspace create my-project --project-path /path/to/project
./build/bengear workspace remove my-project
./build/bengear workspace restore my-project
```

### 会话管理

```bash
./build/bengear session list
./build/bengear session delete <session_id>
```

### 列出技能

```bash
./build/bengear --list-skills
```

## 配置

主配置文件为工作区根目录的 `config.json`。此文件被 Git 忽略，因为它可能包含密钥。使用 `config-example.json` 作为模板。

### 快速开始

```bash
cp config-example.json config.json
```

然后编辑 `config.json`，填入本地 API 密钥和模型名称。

### 最小配置示例

```json
{
  "active_model": "oneapi:deepseek_flash",
  "model_config": {
    "oneapi": {
      "base_url": "https://oneapi.example.com/v1",
      "api_key": "${API_KEY}",
      "models": [
        {
          "id": "DeepSeek-V4-Flash",
          "name": "deepseek_flash",
          "api_mode": "openai",
          "contextWindow": 204800,
          "max_tokens": 131072,
          "temperature": 0.3
        }
      ]
    }
  }
}
```

### URL 自动补全

- OpenAI: `base_url` 自动追加 `/v1/chat/completions`
- Anthropic: `base_url` 自动追加 `/v1/messages`
- 如果 `base_url` 已以 `/v1` 结尾，不会重复添加
- 如果设置了 `api_url`，则使用完整端点

## 下一步

- [配置详解](configuration.md) - 完整配置选项
- [CLI 参考](cli.md) - 所有 CLI 选项和子命令
- [工具参考](tools-reference.md) - 内置工具列表
- [架构设计](architecture.md) - 系统架构和设计原则
