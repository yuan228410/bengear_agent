#include "ben_gear/audit/audit_store.hpp"
#include "ben_gear/test/test_framework.hpp"

using bengear::test::TmpDirTest;

class AuditStoreTest : public TmpDirTest {};

TEST_F(AuditStoreTest, AppendAndListReturnsNewestFirst) {
    ben_gear::audit::AuditStore store(dir() / "audit" / "events.jsonl");

    auto first = store.append(ben_gear::Json{{"workspace", "default"}, {"session_id", "sid-1"}, {"username", "alice"}, {"category", "permission"}, {"action", "requested"}, {"outcome", "pending"}});
    auto second = store.append(ben_gear::Json{{"workspace", "default"}, {"session_id", "sid-1"}, {"username", "alice"}, {"category", "permission"}, {"action", "approved"}, {"outcome", "success"}});

    EXPECT_TRUE(first.value("success", false));
    EXPECT_TRUE(second.value("success", false));

    ben_gear::audit::AuditQuery query;
    query.workspace = ben_gear::base::container::String("default");
    query.session_id = ben_gear::base::container::String("sid-1");
    query.limit = 10;
    auto listed = store.list(query);

    ASSERT_TRUE(listed.value("success", false));
    ASSERT_TRUE(listed["events"].is_array());
    ASSERT_EQ(listed["events"].size(), 2u);
    EXPECT_EQ(listed["events"][0].value("action", ""), "approved");
    EXPECT_EQ(listed["events"][1].value("action", ""), "requested");
    EXPECT_FALSE(listed["events"][0].value("event_id", "").empty());
    EXPECT_FALSE(listed["events"][0].value("ts", "").empty());
}

TEST_F(AuditStoreTest, ListFiltersCategoryActionAndLimit) {
    ben_gear::audit::AuditStore store(dir() / "audit" / "events.jsonl");
    (void)store.append(ben_gear::Json{{"workspace", "default"}, {"session_id", "sid-1"}, {"category", "permission"}, {"action", "requested"}});
    (void)store.append(ben_gear::Json{{"workspace", "default"}, {"session_id", "sid-1"}, {"category", "permission"}, {"action", "approved"}});
    (void)store.append(ben_gear::Json{{"workspace", "other"}, {"session_id", "sid-1"}, {"category", "permission"}, {"action", "approved"}});
    (void)store.append(ben_gear::Json{{"workspace", "default"}, {"session_id", "sid-2"}, {"category", "git"}, {"action", "commit"}});

    ben_gear::audit::AuditQuery query;
    query.workspace = ben_gear::base::container::String("default");
    query.session_id = ben_gear::base::container::String("sid-1");
    query.category = ben_gear::base::container::String("permission");
    query.action = ben_gear::base::container::String("approved");
    query.limit = 1;
    auto listed = store.list(query);

    ASSERT_TRUE(listed.value("success", false));
    ASSERT_TRUE(listed["events"].is_array());
    ASSERT_EQ(listed["events"].size(), 1u);
    EXPECT_EQ(listed["events"][0].value("workspace", ""), "default");
    EXPECT_EQ(listed["events"][0].value("session_id", ""), "sid-1");
    EXPECT_EQ(listed["events"][0].value("category", ""), "permission");
    EXPECT_EQ(listed["events"][0].value("action", ""), "approved");
}
