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

TEST_F(AuditStoreTest, RuntimeExecutionStoreAppendsListsFiltersAndReads) {
    ben_gear::audit::RuntimeExecutionStore store(dir() / "runtime" / "executions.jsonl");
    auto first = store.append(ben_gear::Json{{"workspace", "default"},
                                             {"session_id", "sid-1"},
                                             {"username", "alice"},
                                             {"action", "patch.apply"},
                                             {"status", "succeeded"},
                                             {"operation", ben_gear::Json{{"capability", "patch_apply"}}},
                                             {"execution", ben_gear::Json{{"trace", ben_gear::Json::array()}}}});
    auto second = store.append(ben_gear::Json{{"workspace", "default"},
                                              {"session_id", "sid-1"},
                                              {"username", "alice"},
                                              {"action", "git.commit"},
                                              {"status", "failed"},
                                              {"operation", ben_gear::Json{{"capability", "git_commit"}}}});

    ASSERT_TRUE(first.value("success", false));
    ASSERT_TRUE(second.value("success", false));

    ben_gear::audit::RuntimeExecutionQuery query;
    query.workspace = ben_gear::base::container::String("default");
    query.session_id = ben_gear::base::container::String("sid-1");
    query.username = ben_gear::base::container::String("alice");
    query.status = ben_gear::base::container::String("failed");
    query.capability = ben_gear::base::container::String("git_commit");
    query.limit = 10;
    auto listed = store.list(query);

    ASSERT_TRUE(listed.value("success", false));
    ASSERT_TRUE(listed["executions"].is_array());
    ASSERT_EQ(listed["executions"].size(), 1u);
    EXPECT_EQ(listed["executions"][0].value("action", ""), "git.commit");
    auto execution_id = listed["executions"][0].value("execution_id", "");
    EXPECT_FALSE(execution_id.empty());

    auto read = store.get(ben_gear::base::container::String(execution_id));
    ASSERT_TRUE(read.value("success", false));
    EXPECT_EQ(read["execution"].value("status", ""), "failed");
}

TEST_F(AuditStoreTest, RuntimeExecutionLinkStoreAppendsAndListsBySourceOrTarget) {
    ben_gear::audit::RuntimeExecutionLinkStore store(dir() / "runtime" / "links.jsonl");
    auto first = store.append(ben_gear::Json{{"workspace", "default"},
                                             {"session_id", "sid-1"},
                                             {"username", "alice"},
                                             {"source_execution_id", "exec-failed"},
                                             {"target_execution_id", "exec-patch"},
                                             {"relation", "repair_patch"},
                                             {"repair_plan_id", "plan-1"}});
    auto second = store.append(ben_gear::Json{{"workspace", "default"},
                                              {"session_id", "sid-1"},
                                              {"username", "alice"},
                                              {"source_execution_id", "exec-patch"},
                                              {"target_execution_id", "exec-verify"},
                                              {"relation", "verification_rerun"}});

    ASSERT_TRUE(first.value("success", false));
    ASSERT_TRUE(second.value("success", false));

    ben_gear::audit::RuntimeExecutionLinkQuery query;
    query.workspace = ben_gear::base::container::String("default");
    query.session_id = ben_gear::base::container::String("sid-1");
    query.username = ben_gear::base::container::String("alice");
    query.execution_id = ben_gear::base::container::String("exec-patch");
    query.limit = 10;
    auto listed = store.list(query);

    ASSERT_TRUE(listed.value("success", false));
    ASSERT_EQ(listed["links"].size(), 2u);
    EXPECT_FALSE(listed["links"][0].value("link_id", "").empty());

    query.relation = ben_gear::base::container::String("repair_patch");
    auto repair_links = store.list(query);
    ASSERT_TRUE(repair_links.value("success", false));
    ASSERT_EQ(repair_links["links"].size(), 1u);
    EXPECT_EQ(repair_links["links"][0].value("target_execution_id", ""), "exec-patch");
}
