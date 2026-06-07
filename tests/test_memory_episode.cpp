#include <gtest/gtest.h>
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/memory/compactor.hpp"
#include "ben_gear/memory/context.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/workspace/types.hpp"
#include "test_util.hpp"

using bengear::test::TmpDirTest;

// --- EpisodeStore ---

class EpisodeStoreTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        session_dir_ = dir() / "session-001";
        episode_ = std::make_unique<ben_gear::memory::EpisodeStore>(session_dir_);
    }
    std::filesystem::path session_dir_;
    std::unique_ptr<ben_gear::memory::EpisodeStore> episode_;
};

TEST_F(EpisodeStoreTest, AppendAndRead) {
    namespace container = ben_gear::base::container;
    episode_->append_today(container::String("First event"));
    episode_->append_today(container::String("Second event"));

    auto content = episode_->read_today();
    auto s = std::string(content.data(), content.size());
    EXPECT_NE(s.find("First event"), std::string::npos);
    EXPECT_NE(s.find("Second event"), std::string::npos);
}

// --- Compactor ---

class CompactorTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        namespace ws = ben_gear::workspace;
        paths_ = ws::TierPaths{
            dir() / "global",
            dir() / "users" / "test",
            dir() / "users" / "test" / "workspaces" / "default"
        };
        store_ = std::make_unique<ben_gear::memory::MemoryStore>(paths_);
        episode_ = std::make_unique<ben_gear::memory::EpisodeStore>(
            dir() / "session-001");
        skill_loader_ = std::make_unique<ben_gear::skill::SkillLoader>(
            paths_.global_dir, paths_.user_dir, paths_.workspace_dir);
        ctx_ = std::make_unique<ben_gear::memory::ContextBuilder>(*store_, *skill_loader_);
    }
    ben_gear::workspace::TierPaths paths_;
    std::unique_ptr<ben_gear::memory::MemoryStore> store_;
    std::unique_ptr<ben_gear::memory::EpisodeStore> episode_;
    std::unique_ptr<ben_gear::skill::SkillLoader> skill_loader_;
    std::unique_ptr<ben_gear::memory::ContextBuilder> ctx_;
};

TEST_F(CompactorTest, BelowThresholdNoCompact) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);
    EXPECT_FALSE(compactor.should_compact(100));
}

TEST_F(CompactorTest, AboveHardThresholdCompacts) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);
    EXPECT_TRUE(compactor.should_compact(900));
}

TEST_F(CompactorTest, SoftThresholdNeedsPreviousRound) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    cfg.early_compact_ratio = 0.85;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);
    EXPECT_FALSE(compactor.should_compact(690));
}

TEST_F(CompactorTest, ShortHistoryNoCompact) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);
    ben_gear::llm::ConversationHistory history;
    history.add_user(ben_gear::base::container::String("hello"));
    EXPECT_FALSE(compactor.should_compact_local(history));
}

TEST_F(CompactorTest, CompactPreservesRecentRounds) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    cfg.keep_recent = 2;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    for (int i = 0; i < 5; ++i) {
        history.add_user(ben_gear::base::container::String("user msg " + std::to_string(i)));
        history.add_assistant(ben_gear::base::container::String("assistant reply " + std::to_string(i)));
    }

    auto chat_fn = [](const std::string& /*prompt*/) -> std::string {
        return "<round_0>Summary 0</round_0>\n<round_1>Summary 1</round_1>\n<round_2>Summary 2</round_2>";
    };

    auto result = compactor.compact(std::move(history), chat_fn);
    // 应保留 system + 2 recent rounds
    bool has_system = false;
    for (const auto& msg : result.messages()) {
        if (msg.role == ben_gear::llm::MessageRole::system) has_system = true;
    }
    EXPECT_TRUE(has_system);
    EXPECT_GT(result.messages().size(), 1u);
}

TEST_F(CompactorTest, ExceptionInLLMFallsBack) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.keep_recent = 1;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    for (int i = 0; i < 5; ++i) {
        history.add_user(ben_gear::base::container::String(
            "This is a long user message number " + std::to_string(i) +
            " with enough text to require summarization in the compaction process"));
        history.add_assistant(ben_gear::base::container::String(
            "This is a long assistant reply " + std::to_string(i) +
            " with substantial content to contribute to token usage"));
    }

    auto chat_fn = [](const std::string&) -> std::string {
        throw std::runtime_error("LLM error");
    };

    auto result = compactor.compact(std::move(history), chat_fn);
    bool has_system = false;
    for (const auto& msg : result.messages()) {
        if (msg.role == ben_gear::llm::MessageRole::system) has_system = true;
    }
    EXPECT_TRUE(has_system);
    EXPECT_GT(result.messages().size(), 1u);
}

// --- Compactor: 缓存持久化 ---

TEST_F(CompactorTest, CachePersistence) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 1000;
    cfg.context_usage_threshold = 0.8;
    cfg.keep_budget_ratio = 0.05;
    cfg.keep_recent = 2;
    ben_gear::memory::Compactor compactor1(cfg, *store_, *episode_, *ctx_, dir());

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    for (int i = 0; i < 30; ++i) {
        history.add_user(ben_gear::base::container::String(
            "This is a long user message number " + std::to_string(i) +
            " with enough text to consume token budget for compaction testing purposes. "
            "Adding more text to make each message longer so total tokens exceed the keep budget."));
        history.add_assistant(ben_gear::base::container::String(
            "This is a long assistant reply " + std::to_string(i) +
            " that also has substantial content to contribute to token usage calculations. "
            "More text here to increase token count per round for testing cache persistence."));
    }

    int chat_fn_calls = 0;
    auto chat_fn = [&chat_fn_calls](const std::string& /*prompt*/) -> std::string {
        chat_fn_calls++;
        return "<round_0>Summary 0</round_0>\n<round_1>Summary 1</round_1>\n<round_2>Summary 2</round_2>";
    };

    auto result = compactor1.compact(std::move(history), chat_fn);

    ASSERT_GT(chat_fn_calls, 0) << "compact() did not call chat_fn";

    auto cache_file = dir() / "compactor_cache.json";
    ASSERT_TRUE(std::filesystem::exists(cache_file));

    ben_gear::memory::Compactor compactor2(cfg, *store_, *episode_, *ctx_, dir());
    EXPECT_TRUE(compactor2.should_compact(690));
}

// --- Token Estimation ---

TEST(ContextEstimationTest, AsciiTokenCount) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("abcd");
    EXPECT_EQ(tokens, 1);
}

TEST(ContextEstimationTest, CjkTokenCount) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("你好");
    EXPECT_GE(tokens, 2);
}

TEST(ContextEstimationTest, EmptyString) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("");
    EXPECT_EQ(tokens, 0);
}

TEST(ContextEstimationTest, MixedAsciiAndCjk) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("abc你好");
    EXPECT_GE(tokens, 3);
}
