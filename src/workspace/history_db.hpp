#pragma once

#include <vector>
#include "base/utils/json.hpp"
#include "agent/core/sub_agent_config.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <deque>

namespace ben_gear::workspace {

namespace container = base::container;

/// SQLite 历史数据库
///
/// 表结构：
/// sessions(id, session_id, workspace, name, session_type, parent_id,
///           created_at, updated_at)
/// - session_type: main / sub_agent / workflow
/// - parent_id: 父会话 session_id（子 Agent / workflow 关联主会话）
///
/// messages(id, workspace, session_id, seq, ts, role, content,
///          tool_call_id, tool_name)
class HistoryDB {
public:
 explicit HistoryDB(const std::filesystem::path& db_path);
 ~HistoryDB();

 HistoryDB(const HistoryDB&) = delete;
 HistoryDB& operator=(const HistoryDB&) = delete;

 /// 异步写入消息
 void append(const std::string& workspace,
 const std::string& session_id,
 const std::string& role,
 const std::string& content,
 const std::string& tool_call_id = {},
 const std::string& tool_name = {});

 /// 同步更新消息内容
 void update_latest(const std::string& workspace,
 const std::string& session_id,
 const std::string& role,
 const std::string& content);

 /// 同步等待所有异步写入落盘
 void flush();

 /// 同步加载会话消息
 std::vector<Json> load_session(
 const std::string& workspace,
 const std::string& session_id,
 int limit = 0);

 /// 同步加载会话主消息（仅 user / assistant，供 Web 历史恢复使用）
 std::vector<Json> load_session_chat_messages(
 const std::string& workspace,
 const std::string& session_id,
 int limit = 200);

  /// 异步保存会话结构化状态（通过后台刷盘线程）
  void save_session_state_async(const std::string& workspace,
                                const std::string& session_id,
                                const std::string& state_type,
                                const std::string& state_json);

  /// 同步保存会话结构化状态（plan/todo 等非消息状态）— 已弃用，请用 async 版本
  bool save_session_state(const std::string& workspace,
                          const std::string& session_id,
                          const std::string& state_type,
                          const std::string& state_json);

 /// 加载会话结构化状态
 std::string load_session_state(const std::string& workspace,
 const std::string& session_id,
 const std::string& state_type);

 /// 列出工作空间中的会话（可选按 session_type 过滤）
 std::vector<Json> list_sessions(
 const std::string& workspace,
 agent::SessionType type_filter = agent::SessionType::main);

 /// 列出工作空间中的所有会话（不过滤类型）
 std::vector<Json> list_all_sessions(
 const std::string& workspace);

 /// 查询子会话列表
 std::vector<Json> get_child_sessions(
 const std::string& workspace,
 const std::string& parent_id);

 /// 同步更新会话名称
 bool rename_session(const std::string& workspace,
 const std::string& session_id,
 const std::string& name);

 /// 创建会话元数据（含 session_type + parent_id）
 void create_session(const std::string& workspace,
 const std::string& session_id,
 const std::string& name,
 agent::SessionType session_type = agent::SessionType::main,
 const std::string& parent_id = {});

 /// 同步删除会话
 bool delete_session(const std::string& workspace,
 const std::string& session_id);

 /// 删除工作空间全部会话
 int delete_all_sessions(const std::string& workspace);

 /// 删除 updated_at < before_ts 的会话
 int delete_sessions_before(const std::string& workspace,
 int64_t before_ts);

 /// 删除 updated_at > after_ts 的会话
 int delete_sessions_after(const std::string& workspace,
 int64_t after_ts);

 /// 删除消息内容含 keyword 的会话
 int delete_sessions_by_keyword(const std::string& workspace,
 const std::string& keyword);

 /// 删除会话内 ts < before_ts 的消息
 int delete_messages_before(const std::string& workspace,
 const std::string& session_id,
 int64_t before_ts);

 /// 删除会话内含关键词的消息
 int delete_messages_by_keyword(const std::string& workspace,
 const std::string& session_id,
 const std::string& keyword);

 /// 统计工作空间消息总数
 int64_t count_messages(const std::string& workspace);

 /// 统计单会话消息数
 int64_t count_session_messages(const std::string& workspace,
 const std::string& session_id);

 /// 删除空会话元数据
 int cleanup_empty_sessions(const std::string& workspace);

 /// 搜索消息
 std::vector<Json> search(
 const std::string& keyword,
 const std::string& workspace = {},
 int limit = 20);

 /// 按时间范围查询消息
 std::vector<Json> search_by_time(
 const std::string& workspace,
 int64_t start_ts = 0,
 int64_t end_ts = 0,
 int limit = 50);

 /// 关键词 + 时间范围组合查询
 std::vector<Json> search_keyword_time(
 const std::string& keyword,
 const std::string& workspace = {},
 int64_t start_ts = 0,
 int64_t end_ts = 0,
 int limit = 20);

  struct WriteItem {
    std::string workspace;
    std::string session_id;
    int64_t seq;
    int64_t ts;
    std::string role;
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
  };

  struct StateWriteItem {
    std::string workspace;
    std::string session_id;
    std::string state_type;
    std::string state_json;
  };

  void flush_loop();
  void flush_batch(std::deque<WriteItem>& batch);
  void flush_state_batch(std::deque<StateWriteItem>& batch);
  void upsert_session_meta(const std::string& workspace,
                           const std::string& session_id,
                           int64_t ts);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ben_gear::workspace
