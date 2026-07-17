#include "test_framework.hpp"
#include "base/utils/uuid.hpp"
#include "workspace/history_db.hpp"
#include "test_util.hpp"

#include <thread>
#include <vector>

using bengear::test::TmpDirTest;
namespace container = ben_gear::base::container;

// --- UUID ---

TEST(Uuid, Format) {
    auto id = ben_gear::base::utils::generate_uuid();
    auto s = std::string(id.data(), id.size());
    // 16 位十六进制短 ID
    EXPECT_EQ(s.size(), 16u);
    for (char c : s) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(Uuid, Uniqueness) {
    auto id1 = ben_gear::base::utils::generate_uuid();
    auto id2 = ben_gear::base::utils::generate_uuid();
    EXPECT_NE(std::string(id1.data(), id1.size()), std::string(id2.data(), id2.size()));
}

// --- HistoryDB ---

class HistoryDbTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        db_ = std::make_unique<ben_gear::workspace::HistoryDB>(dir() / "history.db");
    }
    void TearDown() override {
        db_.reset();  // 先关闭 SQLite 连接，释放文件锁
        TmpDirTest::TearDown();
    }
    std::unique_ptr<ben_gear::workspace::HistoryDB> db_;
};

TEST_F(HistoryDbTest, AppendAndLoad) {
    std::string ws("test_workspace");
    std::string sid("session-001");

    db_->append(sid, std::string("user"), std::string("Hello"));
    db_->append(sid, std::string("assistant"), std::string("Hi there!"));
    db_->append(sid, std::string("user"), std::string("How are you?"));
    db_->append(sid, std::string("tool"), std::string("result text"),
    std::string("tc1"), std::string("read_file"));

    db_->flush(); // 等待异步写入落盘
    auto messages = db_->load_session(sid);
    ASSERT_EQ(messages.size(), 4u);
    EXPECT_EQ(messages[0]["role"].get<std::string>(), "user");
    EXPECT_EQ(messages[0]["content"].get<std::string>(), "Hello");
    EXPECT_EQ(messages[1]["role"].get<std::string>(), "assistant");
    EXPECT_EQ(messages[3]["role"].get<std::string>(), "tool");
    EXPECT_EQ(messages[3]["tool_call_id"].get<std::string>(), "tc1");
    EXPECT_EQ(messages[3]["tool_name"].get<std::string>(), "read_file");
}

TEST_F(HistoryDbTest, ListSessions) {
    std::string ws("test_workspace");
    std::string sid("session-001");
    db_->append(sid, std::string("user"), std::string("Hello"));

    db_->flush();
    auto sessions = db_->list_sessions("default", ws);
    EXPECT_FALSE(sessions.empty());
}

TEST_F(HistoryDbTest, Search) {
    std::string ws("test_workspace");
    std::string sid("session-001");
    db_->append(sid, std::string("user"), std::string("Hello world"));

    db_->flush();
    auto results = db_->search(std::string("Hello"), "default");
    EXPECT_FALSE(results.empty());
}

TEST_F(HistoryDbTest, DeleteSession) {
    std::string ws("test_workspace");
    std::string sid("session-001");
    db_->append(sid, std::string("user"), std::string("Hello"));

    EXPECT_TRUE(db_->delete_session(sid));
    auto after_delete = db_->load_session(sid);
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
            auto ws = ("workspace_" + std::to_string(s));
            auto sid = ("session_" + std::to_string(s));
            for (int i = 0; i < messages_per_session; ++i) {
                auto role = (i % 2 == 0) ? "user" : "assistant";
                auto content = "message_" + std::to_string(s) + "_" + std::to_string(i);
                db_->append(sid,
                           std::string(role),
                           content);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    db_->flush();
    // 验证每个会话的完整性
    for (int s = 0; s < num_sessions; ++s) {
        auto ws = ("workspace_" + std::to_string(s));
        auto sid = ("session_" + std::to_string(s));
        auto messages = db_->load_session(sid);
        EXPECT_EQ(messages.size(), static_cast<size_t>(messages_per_session))
            ;
    }
}

TEST_F(HistoryDbTest, ConcurrentSameSessionWrites) {
    // 多个线程并发写入同一个会话
    std::string ws("shared_workspace");
    std::string sid("shared_session");
    constexpr int num_threads = 4;
    constexpr int messages_per_thread = 25;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &ws, &sid, t]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                db_->append(sid,
                           std::string("user"),
                           ("thread_" + std::to_string(t) + "_msg_" + std::to_string(i)));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 确保异步队列全部刷盘
    db_->flush();

    // 所有消息应该都写入成功
    auto messages = db_->load_session(sid);
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
        EXPECT_TRUE(found);
    }
}
