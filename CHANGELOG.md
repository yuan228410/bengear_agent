# Changelog

## 0.2.0 (2026-06-11)

### 新增

- **TLS 抽象层**：`TlsEngine` 接口支持 MbedTLS / OpenSSL / Schannel / none 四后端，CMake `TLS_BACKEND` 编译期选择，`set_global_tls_engine()` 运行时替换
- **压缩抽象层**：`CompressEngine` 接口支持 zlib / none 后端，CMake `COMPRESS_BACKEND` 选择
- **自研轻量测试框架**：`ben_gear/test/test_framework.hpp`，gtest 宏兼容，零外部依赖
- **增量裁剪优化**：`ContextPruner` 冻结区跳过 + 活跃区重算，长对话场景 ~9× 加速
- **Token 缓存**：`ConversationHistory::pruned_tokens()` / `original_tokens()`，增量维护 + 懒计算，消除 5×O(n) 重复扫描

### 变更

- `ContextPruner::prune()` 返回 `PruneResult{messages, hard_pruned, soft_pruned}`，不再内含 `estimate_tokens` 调用
- `ContextPruner` 新增 `compute_depths()` 和 `prune_range_with_depths()` 公共接口
- `ConversationHistory::add_message()` 增量维护 `original_tokens_`
- `Compactor::should_compact_local()` 改用 `history.pruned_tokens()`（更准确，零额外开销）
- `ConversationHistory::invalidate_cache()` / `invalidate_all_cache()` 分两级失效
- 连接池 TLS 类型安全：`void*` + 手动 `SSL_free()` → `unique_ptr<TlsEngine::Session>` RAII
- `HttpClient::Transport` 移除裸 `SSL*`/`SSL_CTX*`，改用 `TlsEngine::Session`
- `zip_extract.cpp` 改用 `global_compress_engine().inflate()`，不再直接 `#include <zlib.h>`
- 测试全部迁移至自研框架，移除 gtest/gmock/glog 第三方源码
- JSON 注释清理：移除 "nlohmann 兼容" 表述，标记为自研实现

### 移除

- `third_party/googletest/`、`third_party/glog/`、`third_party/nlohmann/json.hpp`

### 新增第三方

- `third_party/mbedtls/`（TLS 默认后端，vendor）
- `third_party/zlib/`（压缩后端，vendor）

### 性能

| 场景 | 优化前 | 优化后 | 加速 |
|------|--------|--------|------|
| 3000 msgs 每次请求裁剪+估算 | ~10.9 ms | ~1.2 ms | 9× |
| `estimate_tokens(orig)` | 全量 O(n) | 增量 O(1) | ∞ |
| `should_compact_local` | `estimate_messages_tokens` O(n) | `pruned_tokens()` 懒缓存 | ~100× |

## 0.1.0

初始版本。
