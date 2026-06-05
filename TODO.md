# BenGear TODO

## 高优先级

- [x] MCPClient 并发执行：同 server 串行、不同 server 并行的 MVP 方案
- [x] ConnectionPool 实际启用（HttpClient::request_async 使用 acquire/release）
- [x] 编译 warning 清理
- [x] Agent system_prompt() 通过 ContextBuilder 注入 SOUL/RULES/MEMORY
- [x] --session 恢复时实际调用 restore_from_db
- [x] Bug 修复批次：角色过滤生效、压缩器缓存淘汰、localtime_r、async_connect SO_ERROR、SSE forward、OpenAI 重试、SQLite NULL、EpisodeStore RAII、Store fsync、parse_bool、目录遍历防护
- [x] MCP HTTP transport 实现
- [x] MemoryPool::reset() 实现
- [x] HistoryDB 读写锁优化（shared_mutex）

## 中优先级

- [x] ToolCallManager 并行执行工具（并发度上限 8）
- [x] 记忆工具 write_memory tier 参数选择
- [x] recall 工具 section 级别搜索
- [x] SOUL.md / RULES.md 写入工具（当前只有读取）
- [x] EpisodeStore 日期范围查询修复（YYYYMMDD → YYYY-MM-DD 格式转换）
- [x] MemoryUpdater 单元测试

## 测试

- [x] RoleLoader 单元测试
- [x] 线程安全测试：多线程并发 ToolRegistry
- [x] ThreadSanitizer 验证
- [x] 配置系统集成测试：用户级/工作空间级 .conf + 环境变量
- [x] 多 Session 并发测试

## 暂缓

- [ ] 多 Agent 协作（MessageBus, Blackboard, TaskGraph, Orchestrator, TeammateManager）
- [ ] 配置 JSON overlay（当前只支持 flat .conf）
- [ ] Web UI
- [ ] 插件系统
