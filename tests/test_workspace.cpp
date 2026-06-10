#include <gtest/gtest.h>
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/memory/store.hpp"
#include "test_util.hpp"

using bengear::test::TmpDirTest;

class WorkspaceManagerTest : public TmpDirTest {
protected:
    void SetUp() override {
        TmpDirTest::SetUp();
        auto user_dir = dir() / "users" / "test_user";
        mgr_ = std::make_unique<ben_gear::workspace::WorkspaceManager>(user_dir);
    }
    std::unique_ptr<ben_gear::workspace::WorkspaceManager> mgr_;
};

TEST_F(WorkspaceManagerTest, DefaultAutoCreated) {
    auto ws = mgr_->get(ben_gear::base::container::String("default"));
    ASSERT_TRUE(ws.has_value());
    EXPECT_EQ(std::string(ws->name.data(), ws->name.size()), "default");
}

TEST_F(WorkspaceManagerTest, ListIncludesDefault) {
    auto all = mgr_->list_all();
    EXPECT_FALSE(all.empty());
}

TEST_F(WorkspaceManagerTest, CreateAndGet) {
    auto created = mgr_->create(ben_gear::base::container::String("project1"),
                                ben_gear::base::container::String("/tmp/proj"));
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(std::string(created->name.data(), created->name.size()), "project1");

    auto fetched = mgr_->get(ben_gear::base::container::String("project1"));
    ASSERT_TRUE(fetched.has_value());
}

TEST_F(WorkspaceManagerTest, DuplicateCreateFails) {
    mgr_->create(ben_gear::base::container::String("project1"));
    auto dup = mgr_->create(ben_gear::base::container::String("project1"));
    EXPECT_FALSE(dup.has_value());
}

TEST_F(WorkspaceManagerTest, RemoveAndRestore) {
    mgr_->create(ben_gear::base::container::String("project1"));
    EXPECT_TRUE(mgr_->remove(ben_gear::base::container::String("project1")));
    EXPECT_FALSE(mgr_->get(ben_gear::base::container::String("project1")).has_value());

    auto removed = mgr_->list_removed();
    EXPECT_FALSE(removed.empty());

    EXPECT_TRUE(mgr_->restore(ben_gear::base::container::String("project1")));
    EXPECT_TRUE(mgr_->get(ben_gear::base::container::String("project1")).has_value());
}

TEST_F(WorkspaceManagerTest, CannotRemoveDefault) {
    EXPECT_FALSE(mgr_->remove(ben_gear::base::container::String("default")));
}

TEST_F(WorkspaceManagerTest, TierPathsFor) {
    mgr_->create(ben_gear::base::container::String("project1"));
    auto paths = mgr_->tier_paths_for(ben_gear::base::container::String("project1"));
    EXPECT_EQ(paths.workspace_dir.filename(), "project1");
}

// 按需创建：workspace 创建时不预写模板文件，MemoryStore 读取空文件返回空
TEST_F(WorkspaceManagerTest, MemoryStoreReadsEmptyWhenNoFiles) {
    using namespace ben_gear;
    auto name = base::container::String("memory_test2");
    mgr_->create(name);
    auto paths = mgr_->tier_paths_for(name);
    // 覆盖 global_dir 指向临时目录，隔离全局层 SOUL.md
    paths.global_dir = dir() / "global";
    memory::MemoryStore store(paths);

    // 没有写入任何文件，读取应返回空
    auto soul = store.read_soul();
    EXPECT_EQ(std::string(soul.data(), soul.size()), "");
    auto mem = store.read_memory();
    EXPECT_EQ(std::string(mem.data(), mem.size()), "");
    auto rules = store.read_rules();
    EXPECT_EQ(std::string(rules.data(), rules.size()), "");

    // 写入后能读回
    store.write_soul(base::container::String("I am BenGear"), base::Tier::workspace);
    auto soul2 = store.read_soul();
    EXPECT_NE(std::string(soul2.data(), soul2.size()).find("BenGear"), std::string::npos);
}

// Write through MemoryStore, read back — round-trip at workspace tier
TEST_F(WorkspaceManagerTest, MemoryStoreWriteAndReadRoundTrip) {
    using namespace ben_gear;
    auto name = base::container::String("roundtrip_test");
    mgr_->create(name);
    auto paths = mgr_->tier_paths_for(name);
    memory::MemoryStore store(paths);

    store.write_memory(base::container::String("test memory content"), workspace::Tier::workspace);
    auto mem = store.read_memory();
    EXPECT_NE(std::string(mem.data(), mem.size()).find("test memory content"), std::string::npos);
}
