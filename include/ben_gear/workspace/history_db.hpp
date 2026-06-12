#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/agent/sub_agent_config.hpp"

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
 void append(const container::String& workspace,
 const container::String& session_id,
 const container::String& role,
 const container::String& content,
 const container::String& tool_call_id = {},
 const container::String& tool_name = {});

 /// 同步更新消息内容
 void update_latest(const container::String& workspace,
 const container::String& session_id,
 const container::String& role,
 const container::String& content);

 /// 同步等待所有异步写入落盘
 void flush();

 /// 同步加载会话消息
 container::Vector<Json> load_session(
 const container::String& workspace,
 const container::String& session_id,
 int limit = 0);

 /// 列出工作空间中的会话（可选按 session_type 过滤）
 container::Vector<Json> list_sessions(
 const container::String& workspace,
 agent::SessionType type_filter = agent::SessionType::main);

 /// 列出工作空间中的所有会话（不过滤类型）
 container::Vector<Json> list_all_sessions(
 const container::String& workspace);

 /// 查询子会话列表
 container::Vector<Json> get_child_sessions(
 const container::String& workspace,
 const container::String& parent_id);

 /// 同步更新会话名称
 bool rename_session(const container::String& workspace,
 const container::String& session_id,
 const container::String& name);

 /// 创建会话元数据（含 session_type + parent_id）
 void create_session(const container::String& workspace,
 const container::String& session_id,
 const container::String& name,
 agent::SessionType session_type = agent::SessionType::main,
 const container::String& parent_id = {});

 /// 同步删除会话
 bool delete_session(const container::String& workspace,
 const container::String& session_id);

 /// 删除工作空间全部会话
 int delete_all_sessions(const container::String& workspace);

 /// 删除 updated_at < before_ts 的会话
 int delete_sessions_before(const container::String& workspace,
 int64_t before_ts);

 /// 删除 updated_at > after_ts 的会话
 int delete_sessions_after(const container::String& workspace,
 int64_t after_ts);

 /// 删除消息内容含 keyword 的会话
 int delete_sessions_by_keyword(const container::String& workspace,
 const container::String& keyword);

 /// 删除会话内 ts < before_ts 的消息
 int delete_messages_before(const container::String& workspace,
 const container::String& session_id,
 int64_t before_ts);

 /// 删除会话内含关键词的消息
 int delete_messages_by_keyword(const container::String& workspace,
 const container::String& session_id,
 const container::String& keyword);

 /// 统计工作空间消息总数
 int64_t count_messages(const container::String& workspace);

 /// 统计单会话消息数
 int64_t count_session_messages(const container::String& workspace,
 const container::String& session_id);

 /// 删除空会话元数据
 int cleanup_empty_sessions(const container::String& workspace);

 /// 搜索消息
 container::Vector<Json> search(
 const container::String& keyword,
 const container::String& workspace = {},
 int limit = 20);

 /// 按时间范围查询消息
 container::Vector<Json> search_by_time(
 const container::String& workspace,
 int64_t start_ts = 0,
 int64_t end_ts = 0,
 int limit = 50);

 /// 关键词 + 时间范围组合查询
 container::Vector<Json> search_keyword_time(
 const container::String& keyword,
 const container::String& workspace = {},
 int64_t start_ts = 0,
 int64_t end_ts = 0,
 int limit = 20);

private:
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

 void flush_loop();
 void flush_batch(std::deque<WriteItem>& batch);
 void upsert_session_meta(const std::string& workspace,
 const std::string& session_id,
 int64_t ts);

 struct Impl;
 std::unique_ptr<Impl> impl_;
};

} // namespace ben_gear::workspace
