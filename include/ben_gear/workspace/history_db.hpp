#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

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

/// SQLite 历史数据库（每用户独立数据库文件，无需 user 字段）
///
/// 表结构：
///   sessions(id, session_id, workspace, name, created_at, updated_at)
///     - 会话元数据：名称、创建/更新时间
///     - 首次 append 时自动创建，后续 append 自动更新 updated_at
///
///   messages(id, workspace, session_id, seq, ts, role, content,
///            tool_call_id, tool_name)
///     - seq: 单调递增序列号，保证消息严格有序
///     - ts: Unix 时间戳（秒），范围查询高效
///     - role: user / assistant / thinking / tool / system
///     - tool_call_id / tool_name: 仅 tool 角色使用
///
///   messages_fts: FTS5 外部内容模式，仅索引非 tool 消息
///     - 避免索引工具返回的大量内容
///     - 外部内容模式不重复存储 content
///
/// 写入策略：异步队列 + 批量事务刷盘
/// 读取策略：同步执行，flush() 可确保数据落盘
class HistoryDB {
public:
    explicit HistoryDB(const std::filesystem::path& db_path);
    ~HistoryDB();

    HistoryDB(const HistoryDB&) = delete;
    HistoryDB& operator=(const HistoryDB&) = delete;

    /// 异步写入消息（seq 由内部自增，调用方无需关心）
    void append(const container::String& workspace,
                const container::String& session_id,
                const container::String& role,
                const container::String& content,
                const container::String& tool_call_id = {},
                const container::String& tool_name = {});

    /// 同步更新消息内容（用于流式追加 assistant 消息）
    /// 通过 workspace + session_id + role + seq 定位最新一条匹配记录
    void update_latest(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& role,
                       const container::String& content);

    /// 同步等待所有异步写入落盘（关键场景：恢复会话前调用）
    void flush();

    /// 同步加载会话消息（按 seq 排序，保证严格有序）
    container::Vector<Json> load_session(
        const container::String& workspace,
        const container::String& session_id,
        int limit = 0);

    /// 同步列出工作空间中的会话（从 sessions 表读取元数据）
    container::Vector<Json> list_sessions(
        const container::String& workspace);

    /// 同步更新会话名称
    bool rename_session(const container::String& workspace,
                        const container::String& session_id,
                        const container::String& name);

    /// 同步删除会话
    bool delete_session(const container::String& workspace,
                        const container::String& session_id);

    /// 删除工作空间全部会话，返回删除数
    int delete_all_sessions(const container::String& workspace);

    /// 删除 updated_at < before_ts 的会话，返回删除数
    int delete_sessions_before(const container::String& workspace,
                               int64_t before_ts);

    /// 删除 updated_at > after_ts 的会话，返回删除数
    int delete_sessions_after(const container::String& workspace,
                              int64_t after_ts);

    /// 删除消息内容含 keyword 的会话（FTS5 优先，LIKE 降级），返回删除数
    int delete_sessions_by_keyword(const container::String& workspace,
                                   const container::String& keyword);

    /// 删除会话内 ts < before_ts 的消息，返回删除数
    /// 删完后若会话为空，自动删除 sessions 表对应行
    int delete_messages_before(const container::String& workspace,
                               const container::String& session_id,
                               int64_t before_ts);

    /// 删除会话内含关键词的消息，返回删除数
    /// 删完后若会话为空，自动删除 sessions 表对应行
    int delete_messages_by_keyword(const container::String& workspace,
                                   const container::String& session_id,
                                   const container::String& keyword);

    /// 统计工作空间消息总数
    int64_t count_messages(const container::String& workspace);

    /// 统计单会话消息数
    int64_t count_session_messages(const container::String& workspace,
                                   const container::String& session_id);

    /// 删除空会话元数据（消息数为 0 的 sessions 行）
    int cleanup_empty_sessions(const container::String& workspace);

    /// 同步搜索消息（FTS5 全文检索，仅搜索非 tool 消息，降级到 LIKE）
    container::Vector<Json> search(
        const container::String& keyword,
        const container::String& workspace = {},
        int limit = 20);

    /// 按时间范围查询消息
    /// start_ts/end_ts: Unix 时间戳（秒），0 表示不限
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

}  // namespace ben_gear::workspace
