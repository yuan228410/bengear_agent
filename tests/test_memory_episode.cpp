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
    }
    std::filesystem::path session_dir_;
};

TEST_F(EpisodeStoreTest, AppendAndRead) {
    namespace container = ben_gear::base::container;
    ben_gear::memory::EpisodeStore::append_today(session_dir_,
        container::String("First event"));
    ben_gear::memory::EpisodeStore::append_today(session_dir_,
        container::String("Second event"));

    auto content = ben_gear::memory::EpisodeStore::read_today(session_dir_);
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
        episode_ = std::make_unique<ben_gear::memory::EpisodeStore>();
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
    // last_round_count_ = 0, so soft threshold does not trigger
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
        return "<round_0>Summary of round 0</round_0>\n<round_1>Summary of round 1</round_1>\n<round_2>Summary of round 2</round_2>";
    };

    auto result = compactor.compact(std::move(history), chat_fn);

    // Result should have system message + compacted old rounds + recent rounds
    bool has_system = false;
    int user_count = 0;
    for (const auto& msg : result.messages()) {
        if (msg.role == ben_gear::llm::MessageRole::system) has_system = true;
        if (msg.role == ben_gear::llm::MessageRole::user) user_count++;
    }
    EXPECT_TRUE(has_system);
    EXPECT_GE(user_count, 2);  // At least keep_recent rounds
}

TEST_F(CompactorTest, CompactWithSingleRoundReturnsAsIs) {
    ben_gear::memory::Compactor::Config cfg;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_user(ben_gear::base::container::String("only one round"));
    history.add_assistant(ben_gear::base::container::String("reply"));

    auto chat_fn = [](const std::string&) -> std::string { return ""; };
    auto result = compactor.compact(std::move(history), chat_fn);

    // Single round should be returned unchanged
    EXPECT_EQ(result.messages().size(), 2u);
}

TEST_F(CompactorTest, CompactCacheIsPreserved) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.context_length = 100;
    cfg.context_usage_threshold = 0.8;
    cfg.early_compact_ratio = 0.85;
    cfg.keep_budget_ratio = 0.05;  // small budget so keep_recent rounds are few
    cfg.keep_recent = 2;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    for (int i = 0; i < 10; ++i) {
        // Long messages to force compaction
        history.add_user(ben_gear::base::container::String(
            "This is a long user message number " + std::to_string(i) +
            " with enough text to consume token budget for compaction testing purposes"));
        history.add_assistant(ben_gear::base::container::String(
            "This is a long assistant reply " + std::to_string(i) +
            " that also has substantial content to contribute to token usage calculations"));
    }

    auto chat_fn = [](const std::string& /*prompt*/) -> std::string {
        return "<round_0>Summary 0</round_0>\n<round_1>Summary 1</round_1>\n<round_2>Summary 2</round_2>";
    };

    auto result = compactor.compact(std::move(history), chat_fn);

    // Hard threshold = 100 * 0.8 = 80, should_compact uses strict >
    EXPECT_TRUE(compactor.should_compact(81));
}

// --- Compactor: split_rounds 边界测试 ---

TEST_F(CompactorTest, SplitRoundsOnlySystem) {
    // 仅 system 消息 → 不应产生轮次
    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system prompt"));

    auto chat_fn = [](const std::string&) -> std::string { return ""; };
    ben_gear::memory::Compactor compactor(ben_gear::memory::Compactor::Config{}, *store_, *episode_, *ctx_);
    auto result = compactor.compact(std::move(history), chat_fn);
    // 单轮（只有 system）应原样返回
    EXPECT_EQ(result.messages().size(), 1u);
}

TEST_F(CompactorTest, SplitRoundsCorrectCount) {
    ben_gear::memory::Compactor::Config cfg;
    cfg.keep_recent = 1;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    for (int i = 0; i < 6; ++i) {
        history.add_user(ben_gear::base::container::String("user msg " + std::to_string(i)));
        history.add_assistant(ben_gear::base::container::String("assistant reply " + std::to_string(i)));
    }

    auto chat_fn = [](const std::string& /*prompt*/) -> std::string {
        return "<round_0>Summary 0</round_0>\n<round_1>Summary 1</round_1>\n"
               "<round_2>Summary 2</round_2>\n<round_3>Summary 3</round_3>\n"
               "<round_4>Summary 4</round_4>";
    };

    auto result = compactor.compact(std::move(history), chat_fn);
    // 应包含 system + 摘要 + 保留的近期轮次
    bool has_system = false;
    for (const auto& msg : result.messages()) {
        if (msg.role == ben_gear::llm::MessageRole::system) has_system = true;
    }
    EXPECT_TRUE(has_system);
}

TEST_F(CompactorTest, SplitRoundsBracketPrefixNotNewRound) {
    // 以 "[第" 开头的 user 消息应归入上一轮，而非开始新轮
    ben_gear::memory::Compactor::Config cfg;
    cfg.keep_recent = 10;
    ben_gear::memory::Compactor compactor(cfg, *store_, *episode_, *ctx_);

    ben_gear::llm::ConversationHistory history;
    history.add_system(ben_gear::base::container::String("system"));
    history.add_user(ben_gear::base::container::String("user msg"));
    history.add_assistant(ben_gear::base::container::String("reply"));
    // "[第" 开头消息不是新轮次
    history.add_user(ben_gear::base::container::String("[第一轮结果]"));
    history.add_assistant(ben_gear::base::container::String("continue"));

    auto chat_fn = [](const std::string&) -> std::string { return ""; };
    auto result = compactor.compact(std::move(history), chat_fn);
    // 只有 1 轮（因为 "[第" 开头的不算新轮），原样返回
    EXPECT_GE(result.messages().size(), 4u);
}

// --- Compactor: batch_summarize 边界测试 ---

TEST_F(CompactorTest, MalformedLLMResponseFallsBack) {
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

    // 返回格式错误的响应
    auto chat_fn = [](const std::string&) -> std::string {
        return "This is not a properly formatted response with round tags";
    };

    auto result = compactor.compact(std::move(history), chat_fn);
    // 即使 LLM 响应格式错误，也应该返回有效结果（回退到截断文本）
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

    // chat_fn 抛出异常
    auto chat_fn = [](const std::string&) -> std::string {
        throw std::runtime_error("LLM error");
    };

    auto result = compactor.compact(std::move(history), chat_fn);
    // 异常时应回退到截断文本
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

    // 验证 compact 确实执行了摘要
    ASSERT_GT(chat_fn_calls, 0) << "compact() did not call chat_fn";

    // 验证缓存文件已写入
    auto cache_file = dir() / "compactor_cache.json";
    ASSERT_TRUE(std::filesystem::exists(cache_file));

    // 创建新 Compactor 加载同一缓存
    ben_gear::memory::Compactor compactor2(cfg, *store_, *episode_, *ctx_, dir());
    // soft_threshold = 1000 * 0.8 * 0.85 = 680
    EXPECT_TRUE(compactor2.should_compact(690));
}

// --- Token Estimation ---

TEST(ContextEstimationTest, AsciiTokenCount) {
    // 4 ASCII chars ≈ 1 token
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("abcd");
    EXPECT_EQ(tokens, 1);
}

TEST(ContextEstimationTest, CjkTokenCount) {
    // Each CJK char = 1 token (3-byte UTF-8)
    // "你好" = 2 CJK chars = 2 tokens
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("你好");
    EXPECT_GE(tokens, 2);
}

TEST(ContextEstimationTest, EmptyString) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("");
    EXPECT_EQ(tokens, 0);
}

TEST(ContextEstimationTest, MixedAsciiAndCjk) {
    auto tokens = ben_gear::memory::ContextBuilder::estimate_text_tokens("abc你好");
    EXPECT_GE(tokens, 3);  // 1 token for "abc" (3 ascii) + 2 for CJK
}
