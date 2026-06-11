#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/memory/section_merge.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/updater.hpp"
#include "ben_gear/workspace/types.hpp"
#include "test_util.hpp"

using bengear::test::TmpDirTest;

// --- SectionMerge ---

TEST(SectionMerge, EmptyInput) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> empty;
    auto result = ben_gear::memory::merge_sections(empty);
    EXPECT_TRUE(std::string(result.data(), result.size()).empty());
}

TEST(SectionMerge, SingleLayer) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> texts;
    texts.push_back(ben_gear::base::container::String("## Intro\nHello\n\n## Notes\nWorld\n"));
    auto result = ben_gear::memory::merge_sections(texts);
    auto s = std::string(result.data(), result.size());
    EXPECT_NE(s.find("## Intro"), std::string::npos);
    EXPECT_NE(s.find("## Notes"), std::string::npos);
    EXPECT_NE(s.find("Hello"), std::string::npos);
}

TEST(SectionMerge, LastWins) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> texts;
    texts.push_back(ben_gear::base::container::String("## Config\nbase_url=old\n\n## Logging\nlevel=info\n"));
    texts.push_back(ben_gear::base::container::String("## Config\nbase_url=new\n\n## Extra\nmore stuff\n"));
    auto result = ben_gear::memory::merge_sections(texts);
    auto s = std::string(result.data(), result.size());
    EXPECT_NE(s.find("base_url=new"), std::string::npos);
    EXPECT_EQ(s.find("base_url=old"), std::string::npos);
    EXPECT_NE(s.find("## Logging"), std::string::npos);
    EXPECT_NE(s.find("## Extra"), std::string::npos);
}

TEST(SectionMerge, OverriddenSectionKeepsPosition) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> texts;
    texts.push_back(ben_gear::base::container::String("## Config\nbase_url=old\n\n## Logging\nlevel=info\n"));
    texts.push_back(ben_gear::base::container::String("## Config\nbase_url=new\n\n## Extra\nmore stuff\n"));
    auto result = ben_gear::memory::merge_sections(texts);
    auto s = std::string(result.data(), result.size());
    auto config_pos = s.find("## Config");
    auto logging_pos = s.find("## Logging");
    EXPECT_LT(config_pos, logging_pos);
}

TEST(SectionMerge, PreambleLastWins) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> texts;
    texts.push_back(ben_gear::base::container::String("Preamble v1\n\n## Section A\nContent A\n"));
    texts.push_back(ben_gear::base::container::String("Preamble v2\n\n## Section B\nContent B\n"));
    auto result = ben_gear::memory::merge_sections(texts);
    auto s = std::string(result.data(), result.size());
    EXPECT_NE(s.find("Preamble v2"), std::string::npos);
    EXPECT_EQ(s.find("Preamble v1"), std::string::npos);
}

// --- MemoryStore ---

class MemoryStoreTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        TmpDirTest::SetUp();
        paths_ = ben_gear::workspace::TierPaths{
            dir() / "global",
            dir() / "users" / "test",
            dir() / "users" / "test" / "workspaces" / "default"
        };
        store_ = std::make_unique<ben_gear::memory::MemoryStore>(paths_);
    }
    ben_gear::workspace::TierPaths paths_;
    std::unique_ptr<ben_gear::memory::MemoryStore> store_;
};

TEST_F(MemoryStoreTest, EmptyMemory) {
    auto mem = store_->read_memory();
    EXPECT_TRUE(std::string(mem.data(), mem.size()).empty());
}

TEST_F(MemoryStoreTest, WriteAndRead) {
    store_->write_memory(ben_gear::base::container::String("## Facts\n- sky is blue\n"),
                         ben_gear::workspace::Tier::user);
    auto mem = store_->read_memory();
    auto s = std::string(mem.data(), mem.size());
    EXPECT_NE(s.find("sky is blue"), std::string::npos);
}

TEST_F(MemoryStoreTest, ThreeTierMerge) {
    store_->write_memory(ben_gear::base::container::String("## Facts\n- sky is green\n"),
                         ben_gear::workspace::Tier::global);
    store_->write_memory(ben_gear::base::container::String("## Facts\n- sky is blue\n"),
                         ben_gear::workspace::Tier::user);
    store_->write_memory(ben_gear::base::container::String("## Facts\n- sky is blue\n- water is wet\n"),
                         ben_gear::workspace::Tier::workspace);
    auto merged = store_->read_memory();
    auto s = std::string(merged.data(), merged.size());
    EXPECT_NE(s.find("sky is blue"), std::string::npos);
    EXPECT_EQ(s.find("sky is green"), std::string::npos);
    EXPECT_NE(s.find("water is wet"), std::string::npos);
}

TEST_F(MemoryStoreTest, SoulAndRules) {
    store_->write_soul(ben_gear::base::container::String("You are a helpful assistant.\n"),
                       ben_gear::workspace::Tier::workspace);
    auto soul = store_->read_soul();
    EXPECT_NE(std::string(soul.data(), soul.size()).find("helpful assistant"), std::string::npos);

    store_->write_rules(ben_gear::base::container::String("Always be concise.\n"),
                        ben_gear::workspace::Tier::user);
    auto rules = store_->read_rules();
    EXPECT_NE(std::string(rules.data(), rules.size()).find("concise"), std::string::npos);
}

// --- MemoryUpdater ---

class MemoryUpdaterTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        TmpDirTest::SetUp();
        paths_ = ben_gear::workspace::TierPaths{
            dir() / "global",
            dir() / "users" / "test",
            dir() / "users" / "test" / "workspaces" / "default"
        };
        store_ = std::make_unique<ben_gear::memory::MemoryStore>(paths_);
        session_dir_ = dir() / "session";
    }
    ben_gear::workspace::TierPaths paths_;
    std::unique_ptr<ben_gear::memory::MemoryStore> store_;
    std::filesystem::path session_dir_;
};

TEST_F(MemoryUpdaterTest, UpdateWritesMemoryAndEpisode) {
    ben_gear::memory::EpisodeStore episode_store(session_dir_);
    ben_gear::memory::MemoryUpdater updater(*store_, episode_store, session_dir_);

    ben_gear::base::container::Vector<ben_gear::base::container::String> summaries;
    summaries.push_back(ben_gear::base::container::String("User asked about API design"));

    auto chat_fn = [](const std::string& /*prompt*/) -> std::string {
        return "<episode>Discussed API design patterns</episode>\n"
               "<updated_memory>## API\n- Prefers RESTful patterns</updated_memory>\n";
    };

    updater.update(summaries, chat_fn);

    auto mem = store_->read_memory();
    auto s = std::string(mem.data(), mem.size());
    EXPECT_NE(s.find("RESTful patterns"), std::string::npos);
}

TEST_F(MemoryUpdaterTest, NoUpdateNeededSkipsWrite) {
    ben_gear::memory::EpisodeStore episode_store(session_dir_);
    ben_gear::memory::MemoryUpdater updater(*store_, episode_store, session_dir_);

    store_->write_memory(ben_gear::base::container::String("## Existing\n- Old fact\n"),
                         ben_gear::workspace::Tier::user);

    ben_gear::base::container::Vector<ben_gear::base::container::String> summaries;
    summaries.push_back(ben_gear::base::container::String("Chitchat"));

    auto chat_fn = [](const std::string& /*prompt*/) -> std::string {
        return "<episode>Nothing important</episode>\n"
               "<updated_memory>(no update needed)</updated_memory>\n";
    };

    updater.update(summaries, chat_fn);

    auto mem = store_->read_memory();
    auto s = std::string(mem.data(), mem.size());
    EXPECT_NE(s.find("Old fact"), std::string::npos);
}

TEST_F(MemoryUpdaterTest, EmptySummariesNoOp) {
    ben_gear::memory::EpisodeStore episode_store(session_dir_);
    ben_gear::memory::MemoryUpdater updater(*store_, episode_store, session_dir_);

    ben_gear::base::container::Vector<ben_gear::base::container::String> empty;
    auto chat_fn = [](const std::string&) -> std::string { return ""; };

    updater.update(empty, chat_fn);

    auto mem = store_->read_memory();
    EXPECT_TRUE(std::string(mem.data(), mem.size()).empty());
}
