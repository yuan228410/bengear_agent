#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/tools/history_tools.hpp"

#include <filesystem>
#include <chrono>

using namespace ben_gear::workspace;
namespace container = ben_gear::base::container;

// 测试辅助：创建临时 DB 并插入测试数据
struct TestDB {
    std::filesystem::path db_path;
    std::unique_ptr<HistoryDB> db;
    container::String ws{"test_ws"};

    TestDB() {
        db_path = std::filesystem::temp_directory_path() / ("test_history_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        db = std::make_unique<HistoryDB>(db_path);
    }

    ~TestDB() {
        db.reset();
        std::filesystem::remove(db_path);
    }

    // 添加一个会话（N 条消息）
    void add_session(const std::string& session_id, int msg_count) {
        auto sid = container::String(session_id.c_str());
        for (int i = 0; i < msg_count; ++i) {
            db->append(ws, sid,
                container::String("user"),
                container::String(("message " + std::to_string(i)).c_str()));
        }
        db->flush();
    }

    // 添加一个会话（带关键词内容）
    void add_session_with_content(const std::string& session_id, const std::string& content) {
        auto sid = container::String(session_id.c_str());
        db->append(ws, sid,
            container::String("user"),
            container::String(content.c_str()));
        db->flush();
    }
};

// ==================== CountMessages ====================

TEST(HistoryDBTest, CountMessages) {
    TestDB t;
    EXPECT_EQ(t.db->count_messages(t.ws), 0);

    t.add_session("s1", 5);
    EXPECT_EQ(t.db->count_messages(t.ws), 5);

    t.add_session("s2", 3);
    EXPECT_EQ(t.db->count_messages(t.ws), 8);
}

TEST(HistoryDBTest, CountSessionMessages) {
    TestDB t;
    t.add_session("s1", 5);
    t.add_session("s2", 3);

    auto sid1 = container::String("s1");
    auto sid2 = container::String("s2");
    EXPECT_EQ(t.db->count_session_messages(t.ws, sid1), 5);
    EXPECT_EQ(t.db->count_session_messages(t.ws, sid2), 3);
}

// ==================== DeleteAllSessions ====================

TEST(HistoryDBTest, DeleteAllSessions) {
    TestDB t;
    t.add_session("s1", 3);
    t.add_session("s2", 5);

    EXPECT_EQ(t.db->list_sessions(t.ws).size(), 2u);
    int deleted = t.db->delete_all_sessions(t.ws);
    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(t.db->count_messages(t.ws), 0);
    EXPECT_EQ(t.db->list_sessions(t.ws).size(), 0u);
}

TEST(HistoryDBTest, DeleteAllSessionsEmpty) {
    TestDB t;
    int deleted = t.db->delete_all_sessions(t.ws);
    EXPECT_EQ(deleted, 0);
}

// ==================== DeleteSessionsBefore ====================

TEST(HistoryDBTest, DeleteSessionsBefore) {
    TestDB t;
    t.add_session("old", 2);
    t.add_session("recent", 3);

    // 获取会话列表，找到时间戳
    auto sessions = t.db->list_sessions(t.ws);
    EXPECT_EQ(sessions.size(), 2u);

    // 用一个很早的时间戳，不应删除任何会话
    int deleted = t.db->delete_sessions_before(t.ws, 1);
    EXPECT_EQ(deleted, 0);

    // 用一个很晚的时间戳，删除所有会话
    auto far_future = std::chrono::duration_cast<std::chrono::seconds>(
        (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch()).count();
    deleted = t.db->delete_sessions_before(t.ws, far_future);
    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(t.db->count_messages(t.ws), 0);
}

// ==================== DeleteSessionsAfter ====================

TEST(HistoryDBTest, DeleteSessionsAfter) {
    TestDB t;
    t.add_session("s1", 2);

    // 用一个很晚的时间戳，不应删除
    auto far_future = std::chrono::duration_cast<std::chrono::seconds>(
        (std::chrono::system_clock::now() + std::chrono::hours(24)).time_since_epoch()).count();
    int deleted = t.db->delete_sessions_after(t.ws, far_future);
    EXPECT_EQ(deleted, 0);

    // 用一个很早的时间戳，删除所有
    deleted = t.db->delete_sessions_after(t.ws, 1);
    EXPECT_EQ(deleted, 1);
    EXPECT_EQ(t.db->count_messages(t.ws), 0);
}

// ==================== DeleteSessionsByKeyword ====================

TEST(HistoryDBTest, DeleteSessionsByKeyword) {
    TestDB t;
    t.add_session_with_content("s1", "database optimization tips");
    t.add_session_with_content("s2", "frontend styling");
    t.add_session_with_content("s3", "database migration plan");

    int deleted = t.db->delete_sessions_by_keyword(t.ws, container::String("database"));
    EXPECT_EQ(deleted, 2);

    auto remaining = t.db->list_sessions(t.ws);
    EXPECT_EQ(remaining.size(), 1u);
}

TEST(HistoryDBTest, DeleteSessionsByKeywordNoMatch) {
    TestDB t;
    t.add_session_with_content("s1", "hello world");

    int deleted = t.db->delete_sessions_by_keyword(t.ws, container::String("nonexistent"));
    EXPECT_EQ(deleted, 0);
}

// ==================== DeleteMessagesBefore ====================

TEST(HistoryDBTest, DeleteMessagesBefore) {
    TestDB t;
    t.add_session("s1", 5);
    auto sid = container::String("s1");

    // 用未来时间删除所有消息
    auto far_future = std::chrono::duration_cast<std::chrono::seconds>(
        (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch()).count();
    int deleted = t.db->delete_messages_before(t.ws, sid, far_future);
    EXPECT_EQ(deleted, 5);

    // 会话应为空（自动清理）
    EXPECT_EQ(t.db->count_session_messages(t.ws, sid), 0);
}

TEST(HistoryDBTest, DeleteMessagesBeforePartial) {
    TestDB t;
    auto sid = container::String("s1");

    // 先记录一个时间分界点
    auto boundary_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 2;

    // 插入一条消息（ts < boundary）
    t.db->append(t.ws, sid,
        container::String("user"), container::String("old message"));
    t.db->flush();

    // 等到分界点之后再插入新消息
    std::this_thread::sleep_for(std::chrono::seconds(3));

    t.db->append(t.ws, sid,
        container::String("user"), container::String("new message"));
    t.db->flush();

    // 删除 boundary 之前的消息（应只删掉 old message）
    int deleted = t.db->delete_messages_before(t.ws, sid, boundary_ts);
    EXPECT_EQ(deleted, 1);
    EXPECT_EQ(t.db->count_session_messages(t.ws, sid), 1);
}

// ==================== DeleteMessagesByKeyword ====================

TEST(HistoryDBTest, DeleteMessagesByKeyword) {
    TestDB t;
    auto sid = container::String("s1");

    t.db->append(t.ws, sid, container::String("user"), container::String("secret password is 123"));
    t.db->append(t.ws, sid, container::String("assistant"), container::String("normal response"));
    t.db->append(t.ws, sid, container::String("user"), container::String("another secret here"));
    t.db->flush();

    int deleted = t.db->delete_messages_by_keyword(t.ws, sid, container::String("secret"));
    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(t.db->count_session_messages(t.ws, sid), 1);
}

// ==================== CleanupEmptySessions ====================

TEST(HistoryDBTest, CleanupEmptySessions) {
    TestDB t;
    t.add_session("s1", 3);
    auto sid = container::String("s1");

    // 删除所有消息
    auto far_future = std::chrono::duration_cast<std::chrono::seconds>(
        (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch()).count();
    t.db->delete_messages_before(t.ws, sid, far_future);

    // 会话应已自动清理
    auto sessions = t.db->list_sessions(t.ws);
    EXPECT_EQ(sessions.size(), 0u);
}

// ==================== ParseTimeString ====================

TEST(ParseTimeStringTest, IsoDate) {
    auto ts = ben_gear::tools::parse_time_string("2024-01-15");
    EXPECT_TRUE(ts > 0);
    // 2024-01-15 00:00:00 UTC 的近似值
    EXPECT_TRUE(ts > 1705000000); // 大约 2024-01-11
    EXPECT_TRUE(ts < 1706000000); // 小于 2024-01-23
}

TEST(ParseTimeStringTest, RelativeDays) {
    auto ts_7d = ben_gear::tools::parse_time_string("7d");
    EXPECT_TRUE(ts_7d > 0);

    auto ts_30d = ben_gear::tools::parse_time_string("30d");
    EXPECT_TRUE(ts_30d > 0);

    // 30d 应该比 7d 更早
    EXPECT_TRUE(ts_30d < ts_7d);
}

TEST(ParseTimeStringTest, RelativeHours) {
    auto ts_1h = ben_gear::tools::parse_time_string("1h");
    EXPECT_TRUE(ts_1h > 0);

    // 1h 前应该接近当前时间
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    EXPECT_TRUE(ts_1h > now - 7200);  // 不超过 2 小时前
    EXPECT_TRUE(ts_1h < now);
}

TEST(ParseTimeStringTest, Invalid) {
    EXPECT_EQ(ben_gear::tools::parse_time_string(""), 0);
    EXPECT_EQ(ben_gear::tools::parse_time_string("invalid"), 0);
    EXPECT_EQ(ben_gear::tools::parse_time_string("99999-99-99"), 0);
}

// ==================== Container String Operator+ ====================

TEST(ContainerStringTest, Concatenation) {
    container::String a("hello");
    container::String b(" world");
    auto c = a + b;
    EXPECT_TRUE(c == container::String("hello world"));

    auto d = a + "!";
    EXPECT_TRUE(d == container::String("hello!"));

    auto e = "say " + a;
    EXPECT_TRUE(e == container::String("say hello"));
}

TEST(ContainerStringTest, ConcatenationStdString) {
    container::String a("hello");
    std::string b(" world");
    auto c = a + b;
    EXPECT_TRUE(c == container::String("hello world"));

    auto d = b + a;
    EXPECT_TRUE(d == container::String(" worldhello"));
}

TEST(ContainerStringTest, Append) {
    container::String s("hello");
    s += " ";
    s += container::String("world");
    s += std::string("!");
    EXPECT_TRUE(s == container::String("hello world!"));
}
