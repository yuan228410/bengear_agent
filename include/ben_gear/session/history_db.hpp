#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace ben_gear::session {

namespace container = base::container;

/// SQLite 历史数据库（pimpl 封装，线程安全）
/// 每个用户一个数据库文件，内部 mutex 保护写操作
class HistoryDB {
public:
    explicit HistoryDB(const std::filesystem::path& db_path);
    ~HistoryDB();

    // 禁止拷贝
    HistoryDB(const HistoryDB&) = delete;
    HistoryDB& operator=(const HistoryDB&) = delete;

    /// 写入消息
    int64_t append(const container::String& workspace,
                   const container::String& session_id,
                   const container::String& role,
                   const container::String& content,
                   const container::String& metadata = {});

    /// 加载会话消息
    container::Vector<Json> load_session(
        const container::String& workspace,
        const container::String& session_id,
        int limit = 0);

    /// 列出工作空间中的会话
    container::Vector<Json> list_sessions(
        const container::String& workspace);

    /// 删除会话
    bool delete_session(const container::String& workspace,
                        const container::String& session_id);

    /// 搜索消息
    container::Vector<Json> search(
        const container::String& keyword,
        const container::String& workspace = {},
        int limit = 20);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ben_gear::session
