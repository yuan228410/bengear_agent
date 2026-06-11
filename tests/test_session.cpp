#include <gtest/gtest.h>
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "test_util.hpp"

#include <thread>
#include <vector>

using bengear::test::TmpDirTest;
namespace container = ben_gear::base::container;

// --- UUID ---

TEST(Uuid, Format) {
    auto id = ben_gear::workspace::generate_uuid();
    auto s = std::string(id.data(), id.size());
    // 16 位十六进制短 ID
    EXPECT_EQ(s.size(), 16u);
    for (char c : s) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(Uuid, Uniqueness) {
    auto id1 = ben_gear::workspace::generate_uuid();
    auto id2 = ben_gear::workspace::generate_uuid();
    EXPECT_NE(std::string(id1.data(), id1.size()), std::string(id2.data(), id2.size()));
}

// --- HistoryDB ---

class HistoryDbTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        db_ = std::make_unique<ben_gear::workspace::HistoryDB>(dir() / "history.db");
    }
    std::unique_ptr<ben_gear::workspace::HistoryDB> db_;
};

TEST_F(HistoryDbTest, AppendAndLoad) {
    container::String ws("test_workspace");
    container::String sid("session-001");

    db_->append(ws, sid, container::String("user"), container::String("Hello"));
    db_->append(ws, sid, container::String("assistant"), container::String("Hi there!"));
    db_->append(ws, sid, container::String("user"), container::String("How are you?"));
    db_->append(ws, sid, container::String("tool"), container::String("result text"),
    container::String("tc1"), container::String("read_file"));

    db_->flush(); // 等待异步写入落盘
    auto messages = db_->load_session(ws, sid);
    ASSERT_EQ(messages.size(), 4u);
    EXPECT_EQ(messages[0]["role"].get<std::string>(), "user");
    EXPECT_EQ(messages[0]["content"].get<std::string>(), "Hello");
    EXPECT_EQ(messages[1]["role"].get<std::string>(), "assistant");
    EXPECT_EQ(messages[3]["role"].get<std::string>(), "tool");
    EXPECT_EQ(messages[3]["tool_call_id"].get<std::string>(), "tc1");
    EXPECT_EQ(messages[3]["tool_name"].get<std::string>(), "read_file");
}

TEST_F(HistoryDbTest, ListSessions) {
    container::String ws("test_workspace");
    container::String sid("session-001");
    db_->append(ws, sid, container::String("user"), container::String("Hello"));

    db_->flush();
    auto sessions = db_->list_sessions(ws);
    EXPECT_FALSE(sessions.empty());
}

TEST_F(HistoryDbTest, Search) {
    container::String ws("test_workspace");
    container::String sid("session-001");
    db_->append(ws, sid, container::String("user"), container::String("Hello world"));

    db_->flush();
    auto results = db_->search(container::String("Hello"));
    EXPECT_FALSE(results.empty());
}

TEST_F(HistoryDbTest, DeleteSession) {
    container::String ws("test_workspace");
    container::String sid("session-001");
    db_->append(ws, sid, container::String("user"), container::String("Hello"));

    EXPECT_TRUE(db_->delete_session(ws, sid));
    auto after_delete = db_->load_session(ws, sid);
    EXPECT_TRUE(after_delete.empty());
}

// --- Multi-Session concurrent tests ---

TEST_F(HistoryDbTest, ConcurrentMultiSessionWrites) {
    // 多个会话并发写入同一个 HistoryDB
    constexpr int num_sessions = 4;
    constexpr int messages_per_session = 50;

    std::vector<std::thread> threads;
    threads.reserve(num_sessions);

    for (int s = 0; s < num_sessions; ++s) {
        threads.emplace_back([this, s]() {
            auto ws = container::String(("workspace_" + std::to_string(s)).c_str());
            auto sid = container::String(("session_" + std::to_string(s)).c_str());
            for (int i = 0; i < messages_per_session; ++i) {
                auto role = (i % 2 == 0) ? "user" : "assistant";
                auto content = "message_" + std::to_string(s) + "_" + std::to_string(i);
                db_->append(ws, sid,
                           container::String(role),
                           container::String(content.c_str()));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    db_->flush();
    // 验证每个会话的完整性
    for (int s = 0; s < num_sessions; ++s) {
        auto ws = container::String(("workspace_" + std::to_string(s)).c_str());
        auto sid = container::String(("session_" + std::to_string(s)).c_str());
        auto messages = db_->load_session(ws, sid);
        EXPECT_EQ(messages.size(), static_cast<size_t>(messages_per_session))
            << "session " << s << " has " << messages.size() << " messages";
    }
}

TEST_F(HistoryDbTest, ConcurrentSameSessionWrites) {
    // 多个线程并发写入同一个会话
    container::String ws("shared_workspace");
    container::String sid("shared_session");
    constexpr int num_threads = 4;
    constexpr int messages_per_thread = 25;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &ws, &sid, t]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                db_->append(ws, sid,
                           container::String("user"),
                           container::String(("thread_" + std::to_string(t) + "_msg_" + std::to_string(i)).c_str()));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 确保异步队列全部刷盘
    db_->flush();

    // 所有消息应该都写入成功
    auto messages = db_->load_session(ws, sid);
    EXPECT_EQ(messages.size(), static_cast<size_t>(num_threads * messages_per_thread));

    // 验证内容完整性：检查每条线程的消息都在
    for (int t = 0; t < num_threads; ++t) {
        bool found = false;
        for (const auto& msg : messages) {
            auto content = msg.value("content", "");
            if (content.find("thread_" + std::to_string(t) + "_msg_0") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing messages from thread " << t;
    }
}
