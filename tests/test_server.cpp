#include "test_framework.hpp"

#include "llm/run_outcome.hpp"
#include "server/auth/auth.hpp"
#include "server/core/router.hpp"
#include "server/ws/protocol.hpp"
#include "server/api/git_api.hpp"
#include "server/api/permission_api.hpp"
#include "server/api/patch_api.hpp"
#include "server/api/checkpoint_api.hpp"
#include "server/api/test_loop_api.hpp"
#include "server/api/diagnostic_context_api.hpp"
#include "server/api/diagnostic_repair_api.hpp"
#include "server/api/repo_map_api.hpp"
#include "server/api/code_intel_api.hpp"
#include "server/api/audit_api.hpp"
#include "server/api/runtime_api.hpp"
#include "server/api/workbench_api.hpp"
#include "server/composition/server_composition.hpp"
#include "application/workspace_resolver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace container = ben_gear::base::container;
namespace llm = ben_gear::llm;
namespace server = ben_gear::server;

// ==================== RunOutcome ====================

TEST(RunOutcomeTest, ToolLimitIncludesBudgetDetails) {
    auto out = llm::RunOutcome::tool_limit(
        10, 4, 20, 12, 3, 5, container::String("Total tool call limit reached"));

    EXPECT_EQ(out.status, llm::RunStatus::interrupted);
    EXPECT_EQ(out.reason, llm::RunFinishReason::tool_limit);
    EXPECT_EQ(out.severity, llm::RunSeverity::warning);
    EXPECT_EQ(out.retry.mode, llm::RetryMode::continue_run);
    EXPECT_THAT(out.details_json, testing::HasSubstr("\"max_steps\":10"));
    EXPECT_THAT(out.details_json, testing::HasSubstr("\"steps_used\":4"));
    EXPECT_THAT(out.details_json, testing::HasSubstr("\"max_tool_calls\":20"));
    EXPECT_THAT(out.details_json, testing::HasSubstr("\"tool_calls_in_step\":5"));
}

TEST(RunOutcomeTest, ProviderErrorRetryPolicy) {
    auto retryable = llm::RunOutcome::provider_error(429, container::String("rate limited"));
    EXPECT_TRUE(retryable.retry.available);
    EXPECT_EQ(retryable.retry.mode, llm::RetryMode::retry_same);
    EXPECT_EQ(retryable.retry.after_seconds, 10);
    EXPECT_THAT(retryable.details_json, testing::HasSubstr("\"http_status\":429"));

    auto fatal = llm::RunOutcome::provider_error(400, container::String("bad request"));
    EXPECT_FALSE(fatal.retry.available);
    EXPECT_EQ(fatal.retry.mode, llm::RetryMode::none);
}

TEST(RunOutcomeTest, JsonEscapesMessageAndIncludesDetails) {
    auto out = llm::RunOutcome::internal_error(container::String("bad \"json\"\nline"));
    auto json = llm::to_json(out);

    EXPECT_THAT(json, testing::HasSubstr("\"reason\":\"internal_error\""));
    EXPECT_THAT(json, testing::HasSubstr("bad \\\"json\\\"\\nline"));
    EXPECT_THAT(json, testing::HasSubstr("\"retry\":"));
}

// ==================== WebSocket Protocol ====================

TEST(WsProtocolTest, ChatRoundTripKeepsWorkspaceAndPrompt) {
    auto msg = server::WsMessage::chat(container::String("sid-1"), container::String("hello"));
    msg.strings[container::String("workspace")] = container::String("default");

    auto parsed = server::WsMessage::from_json(msg.to_json());
    EXPECT_EQ(parsed.version, 1);
    EXPECT_EQ(parsed.type, container::String("chat"));
    EXPECT_EQ(parsed.session_id, container::String("sid-1"));
    EXPECT_EQ(parsed.strings[container::String("workspace")], container::String("default"));
    EXPECT_EQ(parsed.strings[container::String("prompt")], container::String("hello"));
}

TEST(WsProtocolTest, DoneWithOutcomeMergesUsageAndOutcome) {
    auto msg = server::WsMessage::done_with_outcome(
        container::String("sid-2"),
        R"({"prompt_tokens":12,"context_length":200})",
        llm::to_json(llm::RunOutcome::timeout(container::String("slow"))),
        1.25,
        0.5);
    msg.strings[container::String("workspace")] = container::String("ws-a");

    auto json = msg.to_json();
    EXPECT_THAT(json, testing::HasSubstr("\"type\":\"done\""));
    EXPECT_THAT(json, testing::HasSubstr("\"workspace\":\"ws-a\""));
    EXPECT_THAT(json, testing::HasSubstr("\"prompt_tokens\":12"));
    EXPECT_THAT(json, testing::HasSubstr("\"outcome\":"));
    EXPECT_THAT(json, testing::HasSubstr("\"reason\":\"timeout\""));
}

TEST(WsProtocolTest, TextDataIsEscapedAsJsonString) {
    auto msg = server::WsMessage::tool_result(
        container::String("sid-3"), container::String("read_file"), "plain \"text\"", 0.25);
    auto json = msg.to_json();

    EXPECT_THAT(json, testing::HasSubstr("\"data\":\"plain \\\"text\\\"\""));
    EXPECT_THAT(json, testing::HasSubstr("\"elapsed\":0.250"));
}

TEST(WsProtocolTest, PlanApplyDecisionKeepsStructuredData) {
    auto msg = server::WsMessage::plan_apply_decision(
        container::String("sid-4"),
        R"({"revision":7,"item_id":"step_1","decision_id":"decision_1","choice_id":"choice_1"})");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, container::String("plan_apply_decision"));
    EXPECT_EQ(parsed.session_id, container::String("sid-4"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"decision_id\":\"decision_1\""));
}

TEST(WsProtocolTest, PermissionStateKeepsStructuredData) {
    auto msg = server::WsMessage::permission_state(
        container::String("sid-5"),
        R"({"success":true,"permissions":[{"permission_id":"perm_1"}]})");
    msg.strings[container::String("workspace")] = container::String("default");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, container::String("permission_state"));
    EXPECT_EQ(parsed.session_id, container::String("sid-5"));
    EXPECT_EQ(parsed.strings[container::String("workspace")], container::String("default"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"permission_id\":\"perm_1\""));
}

TEST(WsProtocolTest, PermissionApproveRoundTripKeepsData) {
    auto msg = server::WsMessage::permission_approve(
        container::String("sid-6"),
        R"({"permission_id":"perm_2","allow_session":true})");
    msg.strings[container::String("workspace")] = container::String("default");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, container::String("permission_approve"));
    EXPECT_EQ(parsed.session_id, container::String("sid-6"));
    EXPECT_EQ(parsed.strings[container::String("workspace")], container::String("default"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"permission_id\":\"perm_2\""));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"allow_session\":true"));
}

// ==================== Router ====================

TEST(RouterTest, MatchesPathParamsByMethod) {
    server::Router router;
    router.add_route("GET", "/api/sessions/:id", [](const server::HttpRequest& req) {
        return server::HttpResponse::ok(std::string("{\"id\":\"")
            + req.params.at(container::String("id")).c_str() + "\"}");
    });

    server::HttpRequest req;
    auto* handler = router.match("GET", "/api/sessions/abc-123", req);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(req.params[container::String("id")], container::String("abc-123"));
    EXPECT_EQ((*handler)(req).body, std::string("{\"id\":\"abc-123\"}"));

    server::HttpRequest wrong_method;
    EXPECT_EQ(router.match("POST", "/api/sessions/abc-123", wrong_method), nullptr);
}

TEST(RouterTest, CorsAllowsConfiguredOrigin) {
    server::Router router;
    container::Vector<container::String> origins;
    origins.push_back(container::String("https://app.test"));
    router.set_cors_origins(origins);

    server::HttpRequest req;
    req.headers[container::String("origin")] = container::String("https://app.test");
    auto resp = server::HttpResponse::ok();
    router.apply_cors(req, resp);

    EXPECT_EQ(resp.headers[container::String("Access-Control-Allow-Origin")], container::String("https://app.test"));
    EXPECT_EQ(resp.headers[container::String("Access-Control-Allow-Methods")], container::String("GET, POST, PUT, DELETE, OPTIONS"));
}

// ==================== Git API ====================

TEST(GitApiTest, StatusParsesWorkspaceAndUsername) {
    server::Router router;
    server::GitApiService svc;
    svc.status = [](const container::String& workspace,
                    const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        auto entries = ben_gear::Json::array();
        entries.push_back(ben_gear::Json{{"path", "src/main.cpp"}, {"xy", " M"}, {"staged", false}, {"unstaged", true}, {"untracked", false}});
        return ben_gear::Json{{"success", true}, {"repo_root", "/repo"}, {"branch", "master"}, {"clean", false}, {"entries", entries}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/git/status", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("\"branch\":\"master\""));
    EXPECT_THAT(resp.body, testing::HasSubstr("src/main.cpp"));
}

TEST(GitApiTest, StatusKeepsNonRepoAsJsonSuccessFalse) {
    server::Router router;
    server::GitApiService svc;
    svc.status = [](const container::String& workspace,
                    const container::String& username) {
        EXPECT_TRUE(workspace.empty());
        EXPECT_EQ(username, container::String("alice"));
        return ben_gear::Json{{"success", false}, {"error_type", "git_not_repo"}, {"message", "not a git repository"}, {"entries", ben_gear::Json::array()}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/git/status", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("git_not_repo"));
    EXPECT_THAT(resp.body, testing::HasSubstr("not a git repository"));
}

TEST(GitApiTest, DiffParsesWorkspacePathAndFlags) {
    server::Router router;
    server::GitApiService svc;
    svc.diff = [](const container::String& workspace,
                  const container::String& username,
                  std::string_view path,
                  bool staged,
                  bool stat,
                  bool preview) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(path, std::string_view("src/main.cpp"));
        EXPECT_TRUE(staged);
        EXPECT_FALSE(stat);
        EXPECT_TRUE(preview);
        return ben_gear::Json{{"success", true}, {"path", std::string(path)}, {"staged", staged}, {"stat", stat}, {"diff", "diff --git a/src/main.cpp b/src/main.cpp\n"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("path")] = container::String("src/main.cpp");
    req.query[container::String("staged")] = container::String("1");
    req.query[container::String("stat")] = container::String("0");
    req.query[container::String("preview")] = container::String("1");
    auto* handler = router.match("GET", "/api/git/diff", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("src/main.cpp"));
}

TEST(GitApiTest, DiffAllowsEmptyPathForWorkspaceDiff) {
    server::Router router;
    server::GitApiService svc;
    svc.diff = [](const container::String& workspace,
                  const container::String& username,
                  std::string_view path,
                  bool staged,
                  bool stat,
                  bool preview) {
        EXPECT_TRUE(workspace.empty());
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(path.empty());
        EXPECT_FALSE(staged);
        EXPECT_FALSE(stat);
        EXPECT_TRUE(preview);
        return ben_gear::Json{{"success", true}, {"diff", ""}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/git/diff", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(GitApiTest, DiffServiceUnavailableReturns500) {
    server::Router router;
    server::GitApiService svc;
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/git/diff", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(GitApiTest, LogParsesWorkspacePathAndLimit) {
    server::Router router;
    server::GitApiService svc;
    svc.log = [](const container::String& workspace,
                 const container::String& username,
                 std::string_view path,
                 int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(path, std::string_view("src/main.cpp"));
        EXPECT_EQ(limit, 12);
        auto commits = ben_gear::Json::array();
        commits.push_back(ben_gear::Json{{"hash", "abcdef"}, {"short_hash", "abcdef"}, {"author", "Alice"}, {"date", "2026-01-01T00:00:00+00:00"}, {"subject", "init"}});
        return ben_gear::Json{{"success", true}, {"limit", limit}, {"path", std::string(path)}, {"commits", commits}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("path")] = container::String("src/main.cpp");
    req.query[container::String("limit")] = container::String("12");
    auto* handler = router.match("GET", "/api/git/log", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("abcdef"));
}

TEST(GitApiTest, LogDefaultsInvalidLimit) {
    server::Router router;
    server::GitApiService svc;
    svc.log = [](const container::String&,
                 const container::String& username,
                 std::string_view path,
                 int limit) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(path.empty());
        EXPECT_EQ(limit, 20);
        return ben_gear::Json{{"success", true}, {"limit", limit}, {"commits", ben_gear::Json::array()}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("limit")] = container::String("bad");
    auto* handler = router.match("GET", "/api/git/log", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(GitApiTest, BranchesParsesWorkspaceAndUsername) {
    server::Router router;
    server::GitApiService svc;
    svc.branches = [](const container::String& workspace,
                      const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        auto branches = ben_gear::Json::array();
        branches.push_back(ben_gear::Json{{"name", "master"}, {"current", true}, {"hash", "abcdef"}, {"upstream", "origin/master"}});
        return ben_gear::Json{{"success", true}, {"action", "list"}, {"branches", branches}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("master"));
}

TEST(GitApiTest, BranchesServiceUnavailableReturns500) {
    server::Router router;
    server::GitApiService svc;
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(GitApiTest, WorktreesParsesWorkspaceAndUsername) {
    server::Router router;
    server::GitApiService svc;
    svc.worktrees = [](const container::String& workspace,
                       const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        auto worktrees = ben_gear::Json::array();
        worktrees.push_back(ben_gear::Json{{"path", "/repo"}, {"head", "abcdef"}, {"branch", "refs/heads/master"}});
        return ben_gear::Json{{"success", true}, {"action", "list"}, {"worktrees", worktrees}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/git/worktrees", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("/repo"));
}

TEST(GitApiTest, WorktreesServiceUnavailableReturns500) {
    server::Router router;
    server::GitApiService svc;
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/git/worktrees", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(GitApiTest, CreateBranchParsesBodyAndCallsService) {
    server::Router router;
    server::GitApiService svc;
    svc.create_branch = [](const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view name,
                           std::string_view start_point,
                           bool force) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(name, std::string_view("feature/test"));
        EXPECT_EQ(start_point, std::string_view("master"));
        EXPECT_FALSE(force);
        return ben_gear::Json{{"success", true}, {"action", "create"}, {"branch", std::string(name)}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test","start_point":"master","force":false})";
    auto* handler = router.match("POST", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("feature/test"));
}

TEST(GitApiTest, CreateBranchDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool create_called = false;
    svc.create_branch = [&create_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, bool) {
        create_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_branch"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(create_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_branch"));
}

TEST(GitApiTest, SwitchBranchParsesBodyAndCallsService) {
    server::Router router;
    server::GitApiService svc;
    svc.switch_branch = [](const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view name,
                           bool force) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(name, std::string_view("feature/test"));
        EXPECT_TRUE(force);
        return ben_gear::Json{{"success", true}, {"action", "switch"}, {"branch", std::string(name)}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test","force":true})";
    auto* handler = router.match("POST", "/api/git/branches/switch", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("feature/test"));
}

TEST(GitApiTest, SwitchBranchMissingSessionReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool switch_called = false;
    svc.switch_branch = [&switch_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        switch_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches/switch", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(switch_called);
}

TEST(GitApiTest, DeleteBranchParsesBodyAndCallsService) {
    server::Router router;
    server::GitApiService svc;
    svc.delete_branch = [](const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view name,
                           bool force) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(name, std::string_view("feature/test"));
        EXPECT_FALSE(force);
        return ben_gear::Json{{"success", true}, {"action", "delete"}, {"branch", std::string(name)}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches/delete", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("feature/test"));
}

TEST(GitApiTest, DeleteBranchDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool delete_called = false;
    svc.delete_branch = [&delete_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        delete_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_delete_branch"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches/delete", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(delete_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_delete_branch"));
}

TEST(GitApiTest, DeleteBranchMissingSessionReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool delete_called = false;
    svc.delete_branch = [&delete_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        delete_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches/delete", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(delete_called);
}

TEST(GitApiTest, CreateBranchMissingNameReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool create_called = false;
    svc.create_branch = [&create_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, bool) {
        create_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1"})";
    auto* handler = router.match("POST", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(create_called);
}

TEST(GitApiTest, BranchMutationRoutesDelegateGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool create_called = false;
    bool switch_called = false;
    svc.create_branch = [&create_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, bool) {
        create_called = true;
        return ben_gear::Json{{"success", true}, {"action", "create"}};
    };
    svc.switch_branch = [&switch_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        switch_called = true;
        return ben_gear::Json{{"success", true}, {"action", "switch"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest create_req;
    create_req.username = container::String("alice");
    create_req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* create = router.match("POST", "/api/git/branches", create_req);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ((*create)(create_req).status, 200);

    server::HttpRequest switch_req;
    switch_req.username = container::String("alice");
    switch_req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* switch_handler = router.match("POST", "/api/git/branches/switch", switch_req);
    ASSERT_NE(switch_handler, nullptr);
    EXPECT_EQ((*switch_handler)(switch_req).status, 200);

    EXPECT_TRUE(create_called);
    EXPECT_TRUE(switch_called);
}

TEST(GitApiTest, RestoreParsesBodyAndCallsService) {
    server::Router router;
    server::GitApiService svc;
    svc.restore = [](const container::String& workspace,
                     const container::String& session_id,
                     const container::String& username,
                     const std::vector<std::string>& paths,
                     bool staged,
                     bool worktree) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(paths.size(), 1u);
        EXPECT_EQ(paths[0], "src/main.cpp");
        EXPECT_FALSE(staged);
        EXPECT_TRUE(worktree);
        return ben_gear::Json{{"success", true}, {"restored", ben_gear::Json::array({"src/main.cpp"})}, {"staged", staged}, {"worktree", worktree}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":["src/main.cpp"],"staged":false,"worktree":true})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("src/main.cpp"));
}

TEST(GitApiTest, RestoreDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, const std::vector<std::string>&, bool, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_restore"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(restore_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_restore"));
}

TEST(GitApiTest, RestoreMissingSessionReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, const std::vector<std::string>&, bool, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(restore_called);
}

TEST(GitApiTest, RestoreMissingPathsReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, const std::vector<std::string>&, bool, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":[]})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(restore_called);
}

TEST(GitApiTest, RestoreRouteDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, const std::vector<std::string>&, bool, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", true}, {"restored", ben_gear::Json::array({"src/main.cpp"})}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(restore_called);
}

TEST(GitApiTest, CommitParsesBodyAndCallsService) {
    server::Router router;
    server::GitApiService svc;
    svc.commit = [](const container::String& workspace,
                    const container::String& session_id,
                    const container::String& username,
                    std::string_view message,
                    const std::vector<std::string>& paths,
                    bool all,
                    bool amend) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(message, std::string_view("update file"));
        EXPECT_EQ(paths.size(), 1u);
        EXPECT_EQ(paths[0], "src/main.cpp");
        EXPECT_FALSE(all);
        EXPECT_FALSE(amend);
        return ben_gear::Json{{"success", true}, {"short_hash", "abc123"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":" update file ","paths":["src/main.cpp"],"all":false,"amend":false})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("abc123"));
}

TEST(GitApiTest, CommitDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_commit"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":"update file","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(commit_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_commit"));
}

TEST(GitApiTest, CommitMissingSessionReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","message":"update file","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(commit_called);
}

TEST(GitApiTest, CommitMissingMessageReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":"   ","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(commit_called);
}

TEST(GitApiTest, CommitPathsAndAllConflictReturns400) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":"update file","paths":["src/main.cpp"],"all":true})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(commit_called);
}

TEST(GitApiTest, CommitRouteDelegatesGovernanceToService) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", true}, {"short_hash", "abc123"}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":"update file","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(commit_called);
}

TEST(PermissionApiTest, ListParsesWorkspaceSessionAndUsername) {
    server::Router router;
    server::PermissionApiService svc;
    svc.list_pending = [](const container::String& workspace,
                          const container::String& session_id,
                          const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        auto permissions = ben_gear::Json::array();
        permissions.push_back(ben_gear::Json{{"permission_id", "perm_1"}, {"tool_name", "apply_patch"}, {"policy_key", "patch.apply"}});
        return ben_gear::Json{{"success", true}, {"permissions", permissions}};
    };
    server::register_permission_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("session_id")] = container::String("sid-1");
    auto* handler = router.match("GET", "/api/permissions", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_1"));
}

TEST(PermissionApiTest, ListMissingSessionReturns400) {
    server::Router router;
    server::PermissionApiService svc;
    svc.list_pending = [](const container::String&, const container::String&, const container::String&) {
        return ben_gear::Json{{"success", true}, {"permissions", ben_gear::Json::array()}};
    };
    server::register_permission_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/permissions", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
}

TEST(PermissionApiTest, ApproveParsesBodyAndPathPermissionId) {
    server::Router router;
    server::PermissionApiService svc;
    svc.approve = [](const container::String& workspace,
                     const container::String& session_id,
                     const container::String& username,
                     std::string_view permission_id,
                     bool allow_session) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(permission_id, std::string_view("perm_2"));
        EXPECT_TRUE(allow_session);
        return ben_gear::Json{{"success", true}, {"permission_id", std::string(permission_id)}, {"policy_key", "git.commit"}, {"allow_session", allow_session}};
    };
    server::register_permission_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","allow_session":true})";
    auto* handler = router.match("POST", "/api/permissions/perm_2/approve", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("git.commit"));
}

TEST(PermissionApiTest, DenyNotFoundReturns404) {
    server::Router router;
    server::PermissionApiService svc;
    svc.deny = [](const container::String&,
                  const container::String& session_id,
                  const container::String& username,
                  std::string_view permission_id) {
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(permission_id, std::string_view("missing"));
        return ben_gear::Json{{"success", false}, {"error_type", "permission_not_found"}, {"message", "pending permission not found"}};
    };
    server::register_permission_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"session_id":"sid-1"})";
    auto* handler = router.match("POST", "/api/permissions/missing/deny", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 404);
}

// ==================== Checkpoint API ====================

TEST(CheckpointApiTest, ListParsesWorkspaceSessionAndUsername) {
    server::Router router;
    server::CheckpointApiService svc;
    svc.list = [](const container::String& workspace,
                  const container::String& session_id,
                  const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        auto checkpoints = ben_gear::Json::array();
        checkpoints.push_back(ben_gear::Json{{"checkpoint_id", "chk_1"}, {"description", "before edit"}, {"files", 2}});
        return ben_gear::Json{{"success", true}, {"checkpoints", checkpoints}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("session_id")] = container::String("sid-1");
    auto* handler = router.match("GET", "/api/checkpoints", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("chk_1"));
}

TEST(CheckpointApiTest, ReadParsesIdAndStripsContent) {
    server::Router router;
    server::CheckpointApiService svc;
    svc.read = [](const container::String& workspace,
                  const container::String& session_id,
                  const container::String& username,
                  std::string_view checkpoint_id) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(checkpoint_id, std::string_view("chk_1"));
        auto files = ben_gear::Json::array();
        files.push_back(ben_gear::Json{{"path", "file.txt"}, {"content", "secret"}, {"size", 6}});
        return ben_gear::Json{{"success", true}, {"checkpoint", ben_gear::Json{{"checkpoint_id", "chk_1"}, {"files", files}}}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("session_id")] = container::String("sid-1");
    auto* handler = router.match("GET", "/api/checkpoints/chk_1", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("file.txt"));
    EXPECT_THAT(resp.body, testing::Not(testing::HasSubstr("secret")));
}

TEST(CheckpointApiTest, RestoreParsesBodyAndCallsService) {
    server::Router router;
    server::CheckpointApiService svc;
    svc.restore = [](const container::String& workspace,
                     const container::String& session_id,
                     const container::String& username,
                     std::string_view checkpoint_id,
                     const std::vector<std::string>& paths,
                     bool force) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(checkpoint_id, std::string_view("chk_1"));
        EXPECT_EQ(paths.size(), 1u);
        EXPECT_EQ(paths[0], "file.txt");
        EXPECT_TRUE(force);
        return ben_gear::Json{{"success", true}, {"checkpoint_id", "chk_1"}, {"restored", ben_gear::Json::array({"file.txt"})}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":["file.txt"],"force":true})";
    auto* handler = router.match("POST", "/api/checkpoints/chk_1/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("file.txt"));
}

TEST(CheckpointApiTest, RestoreDelegatesGovernanceToService) {
    server::Router router;
    server::CheckpointApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_restore_checkpoint"}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1"})";
    auto* handler = router.match("POST", "/api/checkpoints/chk_1/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(restore_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
}

TEST(CheckpointApiTest, DeleteParsesBodyAndCallsService) {
    server::Router router;
    server::CheckpointApiService svc;
    svc.remove = [](const container::String& workspace,
                    const container::String& session_id,
                    const container::String& username,
                    std::string_view checkpoint_id) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(checkpoint_id, std::string_view("chk_1"));
        return ben_gear::Json{{"success", true}, {"checkpoint_id", std::string(checkpoint_id)}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1"})";
    auto* handler = router.match("DELETE", "/api/checkpoints/chk_1", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("chk_1"));
}

TEST(CheckpointApiTest, DeleteDelegatesGovernanceToService) {
    server::Router router;
    server::CheckpointApiService svc;
    bool delete_called = false;
    svc.remove = [&delete_called](const container::String&, const container::String&, const container::String&, std::string_view) {
        delete_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_delete_checkpoint"}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1"})";
    auto* handler = router.match("DELETE", "/api/checkpoints/chk_1", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(delete_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
}

TEST(CheckpointApiTest, RestoreMissingSessionReturns400) {
    server::Router router;
    server::CheckpointApiService svc;
    bool restore_called = false;
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_checkpoint_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default"})";
    auto* handler = router.match("POST", "/api/checkpoints/chk_1/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(restore_called);
}

// ==================== Test Loop API ====================

TEST(TestLoopApiTest, InspectParsesWorkspaceAndUsername) {
    server::Router router;
    server::TestLoopApiService svc;
    svc.inspect = [](const container::String& workspace,
                     const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        auto suggestions = ben_gear::Json::array();
        suggestions.push_back(ben_gear::Json{{"id", "cmake-test"}, {"command", "ctest --test-dir build --output-on-failure"}, {"cwd", "."}, {"reason", "CMake project detected"}, {"confidence", 70}});
        return ben_gear::Json{{"success", true}, {"project_root", "/repo"}, {"suggestions", suggestions}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/test-loop/inspect", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("cmake-test"));
    EXPECT_THAT(resp.body, testing::HasSubstr("/repo"));
}

TEST(TestLoopApiTest, InspectServiceUnavailableReturns500) {
    server::Router router;
    server::TestLoopApiService svc;
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/test-loop/inspect", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(TestLoopApiTest, RunParsesBodyAndCallsService) {
    server::Router router;
    server::TestLoopApiService svc;
    svc.run = [](const container::String& workspace,
                 const container::String& session_id,
                 const container::String& username,
                 std::string_view command,
                 std::string_view cwd,
                 int timeout_seconds,
                 int max_output_bytes) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(command, std::string_view("ctest --output-on-failure"));
        EXPECT_EQ(cwd, std::string_view("build"));
        EXPECT_EQ(timeout_seconds, 45);
        EXPECT_EQ(max_output_bytes, 12000);
        return ben_gear::Json{{"success", true}, {"exit_code", 0}, {"elapsed_ms", 123}, {"output", "ok"}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","command":" ctest --output-on-failure ","cwd":"build","timeout_seconds":45,"max_output_bytes":12000})";
    auto* handler = router.match("POST", "/api/test-loop/run", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("ok"));
}

TEST(TestLoopApiTest, RunDelegatesGovernanceToService) {
    server::Router router;
    server::TestLoopApiService svc;
    bool run_called = false;
    svc.run = [&run_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, int, int) {
        run_called = true;
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm_run_tests"}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","command":"ctest"})";
    auto* handler = router.match("POST", "/api/test-loop/run", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(run_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_run_tests"));
}

TEST(TestLoopApiTest, RunMissingSessionReturns400) {
    server::Router router;
    server::TestLoopApiService svc;
    bool run_called = false;
    svc.run = [&run_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, int, int) {
        run_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","command":"ctest"})";
    auto* handler = router.match("POST", "/api/test-loop/run", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(run_called);
}

TEST(TestLoopApiTest, RunMissingCommandReturns400) {
    server::Router router;
    server::TestLoopApiService svc;
    bool run_called = false;
    svc.run = [&run_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, int, int) {
        run_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","command":"   "})";
    auto* handler = router.match("POST", "/api/test-loop/run", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(run_called);
}

TEST(TestLoopApiTest, RunRouteDelegatesGovernanceToService) {
    server::Router router;
    server::TestLoopApiService svc;
    bool run_called = false;
    svc.run = [&run_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, int, int) {
        run_called = true;
        return ben_gear::Json{{"success", true}, {"exit_code", 0}};
    };
    server::register_test_loop_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","command":"ctest"})";
    auto* handler = router.match("POST", "/api/test-loop/run", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(run_called);
}

// ==================== Repo Map API ====================

TEST(RepoMapApiTest, OverviewParsesWorkspaceAndUsername) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.overview = [](const container::String& workspace,
                      const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        return ben_gear::Json{{"success", true}, {"summary", ben_gear::Json{{"project_root", "/repo"}}}, {"important_files", ben_gear::Json::array()}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/repo-map/overview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("/repo"));
}

TEST(RepoMapApiTest, FindFilesParsesQueryFiltersAndLimit) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.find_files = [](const container::String& workspace,
                        const container::String& username,
                        std::string_view query,
                        std::string_view kind,
                        std::string_view language,
                        int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(query, std::string_view("server"));
        EXPECT_EQ(kind, std::string_view("source"));
        EXPECT_EQ(language, std::string_view("cpp"));
        EXPECT_EQ(limit, 12);
        auto files = ben_gear::Json::array();
        files.push_back(ben_gear::Json{{"path", "src/server.cpp"}, {"kind", "source"}, {"language", "cpp"}});
        return ben_gear::Json{{"success", true}, {"files", files}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("query")] = container::String("server");
    req.query[container::String("kind")] = container::String("source");
    req.query[container::String("language")] = container::String("cpp");
    req.query[container::String("limit")] = container::String("12");
    auto* handler = router.match("GET", "/api/repo-map/files", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("src/server.cpp"));
}

TEST(RepoMapApiTest, FindSymbolsParsesQueryFiltersAndLimit) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.find_symbols = [](const container::String& workspace,
                          const container::String& username,
                          std::string_view query,
                          std::string_view kind,
                          std::string_view language,
                          int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(query, std::string_view("Router"));
        EXPECT_EQ(kind, std::string_view("class"));
        EXPECT_EQ(language, std::string_view("cpp"));
        EXPECT_EQ(limit, 8);
        auto symbols = ben_gear::Json::array();
        symbols.push_back(ben_gear::Json{{"name", "Router"}, {"kind", "class"}, {"path", "router.hpp"}});
        return ben_gear::Json{{"success", true}, {"symbols", symbols}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("query")] = container::String("Router");
    req.query[container::String("kind")] = container::String("class");
    req.query[container::String("language")] = container::String("cpp");
    req.query[container::String("limit")] = container::String("8");
    auto* handler = router.match("GET", "/api/repo-map/symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("Router"));
}

TEST(RepoMapApiTest, ExplainParsesPath) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.explain_path = [](const container::String& workspace,
                          const container::String& username,
                          std::string_view path) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(path, std::string_view("src/server.cpp"));
        return ben_gear::Json{{"success", true}, {"file", ben_gear::Json{{"path", std::string(path)}}}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("path")] = container::String("src/server.cpp");
    auto* handler = router.match("GET", "/api/repo-map/explain", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("src/server.cpp"));
}

TEST(RepoMapApiTest, ExplainMissingPathReturns400) {
    server::Router router;
    server::RepoMapApiService svc;
    bool explain_called = false;
    svc.explain_path = [&explain_called](const container::String&, const container::String&, std::string_view) {
        explain_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/repo-map/explain", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(explain_called);
}

TEST(RepoMapApiTest, OverviewServiceUnavailableReturns500) {
    server::Router router;
    server::RepoMapApiService svc;
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/repo-map/overview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

// ==================== Code Intelligence API ====================

TEST(CodeIntelApiTest, CapabilitiesParsesWorkspaceAndUsername) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.capabilities = [](const container::String& workspace,
                          const container::String& username) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        return ben_gear::Json{{"success", true}, {"provider", "indexed"}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    auto* handler = router.match("GET", "/api/code-intel/capabilities", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("indexed"));
}

TEST(CodeIntelApiTest, DocumentSymbolsParsesPath) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.document_symbols = [](const container::String& workspace,
                              const container::String& username,
                              std::string_view path) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(path, std::string_view("src/server.cpp"));
        return ben_gear::Json{{"success", true}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("path")] = container::String("src/server.cpp");
    auto* handler = router.match("GET", "/api/code-intel/document-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, DocumentSymbolsRequiresPath) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.document_symbols = [&called](const container::String&, const container::String&, std::string_view) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/code-intel/document-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(CodeIntelApiTest, WorkspaceSymbolsParsesFiltersAndLimit) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.workspace_symbols = [](const container::String& workspace,
                               const container::String& username,
                               std::string_view query,
                               std::string_view kind,
                               std::string_view language,
                               int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(query, std::string_view("Router"));
        EXPECT_EQ(kind, std::string_view("class"));
        EXPECT_EQ(language, std::string_view("cpp"));
        EXPECT_EQ(limit, 9);
        return ben_gear::Json{{"success", true}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("query")] = container::String("Router");
    req.query[container::String("kind")] = container::String("class");
    req.query[container::String("language")] = container::String("cpp");
    req.query[container::String("limit")] = container::String("9");
    auto* handler = router.match("GET", "/api/code-intel/workspace-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, WorkspaceSymbolsAllowsEmptyQuery) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.workspace_symbols = [&called](const container::String&,
                                      const container::String&,
                                      std::string_view query,
                                      std::string_view,
                                      std::string_view,
                                      int limit) {
        called = true;
        EXPECT_TRUE(query.empty());
        EXPECT_EQ(limit, 50);
        return ben_gear::Json{{"success", true}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/code-intel/workspace-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(called);
}

TEST(CodeIntelApiTest, DefinitionAcceptsSymbol) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.definition = [](const container::String& workspace,
                        const container::String& username,
                        std::string_view path,
                        int line,
                        int column,
                        std::string_view symbol,
                        int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(path.empty());
        EXPECT_EQ(line, 0);
        EXPECT_EQ(column, 0);
        EXPECT_EQ(symbol, std::string_view("Router"));
        EXPECT_EQ(limit, 7);
        return ben_gear::Json{{"success", true}, {"symbol", "Router"}, {"definitions", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("symbol")] = container::String("Router");
    req.query[container::String("limit")] = container::String("7");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("Router"));
}

TEST(CodeIntelApiTest, DefinitionAcceptsPosition) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.definition = [](const container::String&,
                        const container::String&,
                        std::string_view path,
                        int line,
                        int column,
                        std::string_view symbol,
                        int) {
        EXPECT_EQ(path, std::string_view("src/router.cpp"));
        EXPECT_EQ(line, 12);
        EXPECT_EQ(column, 5);
        EXPECT_TRUE(symbol.empty());
        return ben_gear::Json{{"success", true}, {"definitions", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("path")] = container::String("src/router.cpp");
    req.query[container::String("line")] = container::String("12");
    req.query[container::String("column")] = container::String("5");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, DefinitionRequiresSymbolOrPosition) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.definition = [&called](const container::String&, const container::String&, std::string_view, int, int, std::string_view, int) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("path")] = container::String("src/router.cpp");
    req.query[container::String("line")] = container::String("0");
    req.query[container::String("column")] = container::String("5");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(CodeIntelApiTest, ReferencesParsesLimit) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.references = [](const container::String&,
                        const container::String&,
                        std::string_view,
                        int,
                        int,
                        std::string_view symbol,
                        int limit) {
        EXPECT_EQ(symbol, std::string_view("Router"));
        EXPECT_EQ(limit, 3);
        return ben_gear::Json{{"success", true}, {"references", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("symbol")] = container::String("Router");
    req.query[container::String("limit")] = container::String("3");
    auto* handler = router.match("GET", "/api/code-intel/references", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, ServiceUnavailableReturns500) {
    server::Router router;
    server::CodeIntelApiService svc;
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/code-intel/capabilities", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}


TEST(CodeIntelApiTest, ContextPackParsesBody) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.context_pack = [](const container::String& workspace,
                          const container::String& username,
                          const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        return ben_gear::Json{{"success", true}, {"context_pack", ben_gear::Json{{"context_pack_id", "ctx-1"}}}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.body = R"({"diagnostics":[],"paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/code-intel/context-pack", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("ctx-1"));
}

TEST(CodeIntelApiTest, ReadContextPackParsesId) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.read_context_pack = [](const container::String& username,
                               std::string_view context_pack_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(context_pack_id, std::string_view("ctx-1"));
        return ben_gear::Json{{"success", true}, {"context_pack", ben_gear::Json{{"context_pack_id", "ctx-1"}}}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/code-intel/context-packs/ctx-1", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("ctx-1"));
}

// ==================== Diagnostic Context API ====================

TEST(DiagnosticContextApiTest, RepairContextParsesWorkspaceAndBody) {
    server::Router router;
    server::DiagnosticContextApiService svc;
    svc.repair_context = [](const container::String& workspace,
                            const container::String& username,
                            const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        EXPECT_FALSE(request.contains("workspace"));
        EXPECT_EQ(request.value("context_lines", 0), 2);
        return ben_gear::Json{{"success", true}, {"contexts", ben_gear::Json::array()}, {"diagnostic_count", 0}};
    };
    server::register_diagnostic_context_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","diagnostics":[],"context_lines":2})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-context", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("diagnostic_count"));
}

TEST(DiagnosticContextApiTest, RepairContextRejectsInvalidJson) {
    server::Router router;
    server::DiagnosticContextApiService svc;
    bool called = false;
    svc.repair_context = [&called](const container::String&, const container::String&, const ben_gear::Json&) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_diagnostic_context_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"(["bad"])";
    auto* handler = router.match("POST", "/api/diagnostics/repair-context", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(DiagnosticContextApiTest, RepairContextServiceUnavailableReturns500) {
    server::Router router;
    server::DiagnosticContextApiService svc;
    server::register_diagnostic_context_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-context", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(DiagnosticContextApiTest, RepairContextDoesNotRequireRunTestsPermission) {
    server::Router router;
    server::DiagnosticContextApiService svc;
    int calls = 0;
    svc.repair_context = [&calls](const container::String&, const container::String&, const ben_gear::Json&) {
        ++calls;
        return ben_gear::Json{{"success", true}, {"contexts", ben_gear::Json::array()}};
    };
    server::register_diagnostic_context_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-context", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(calls, 1);
}

// ==================== Diagnostic Repair API ====================

TEST(DiagnosticRepairApiTest, RepairPlanParsesWorkspaceAndBody) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    svc.repair_plan = [](const container::String& workspace,
                         const container::String& username,
                         const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        EXPECT_FALSE(request.contains("workspace"));
        EXPECT_EQ(request.value("context_lines", 0), 2);
        return ben_gear::Json{{"success", true}, {"plans", ben_gear::Json::array()}, {"plan_count", 0}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","diagnostics":[],"context_lines":2})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-plan", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("plan_count"));
}

TEST(DiagnosticRepairApiTest, RepairPlanRejectsInvalidJson) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    bool called = false;
    svc.repair_plan = [&called](const container::String&, const container::String&, const ben_gear::Json&) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"(["bad"])";
    auto* handler = router.match("POST", "/api/diagnostics/repair-plan", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(DiagnosticRepairApiTest, RepairPlanServiceUnavailableReturns500) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-plan", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(DiagnosticRepairApiTest, RepairPlanDoesNotRequireRunTestsPermission) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    int calls = 0;
    svc.repair_plan = [&calls](const container::String&, const container::String&, const ben_gear::Json&) {
        ++calls;
        return ben_gear::Json{{"success", true}, {"plans", ben_gear::Json::array()}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-plan", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(calls, 1);
}

TEST(DiagnosticRepairApiTest, PatchPreviewParsesWorkspaceAndBody) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    svc.repair_patch_preview = [](const container::String& workspace,
                                  const container::String& username,
                                  const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        EXPECT_FALSE(request.contains("workspace"));
        EXPECT_EQ(request.value("unified_diff", ""), "--- a/a.txt\n+++ b/a.txt\n");
        return ben_gear::Json{{"success", true}, {"provider", "diagnostic_repair_patch_preview"}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","diagnostics":[],"unified_diff":"--- a/a.txt\n+++ b/a.txt\n"})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-patch-preview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("diagnostic_repair_patch_preview"));
}

TEST(DiagnosticRepairApiTest, PatchPreviewMissingUnifiedDiffReturns400) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    svc.repair_patch_preview = [](const container::String&, const container::String&, const ben_gear::Json&) {
        return ben_gear::Json{{"success", false}, {"error_type", "invalid_arguments"}, {"message", "unified_diff is required"}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-patch-preview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_THAT(resp.body, testing::HasSubstr("invalid_arguments"));
}

TEST(DiagnosticRepairApiTest, PatchPreviewServiceUnavailableReturns500) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[],"unified_diff":"--- a/a.txt\n+++ b/a.txt\n"})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-patch-preview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

TEST(DiagnosticRepairApiTest, PatchPreviewDoesNotRequireRunTestsPermission) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    int calls = 0;
    svc.repair_patch_preview = [&calls](const container::String&, const container::String&, const ben_gear::Json&) {
        ++calls;
        return ben_gear::Json{{"success", true}, {"provider", "diagnostic_repair_patch_preview"}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[],"unified_diff":"--- a/a.txt\n+++ b/a.txt\n"})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-patch-preview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(calls, 1);
}


TEST(DiagnosticRepairApiTest, RepairWorkflowParsesWorkspaceAndBody) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    svc.repair_workflow = [](const container::String& workspace,
                             const container::String& username,
                             const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        EXPECT_TRUE(request.contains("patch_candidates"));
        EXPECT_FALSE(request.contains("workspace"));
        return ben_gear::Json{{"success", true}, {"provider", "diagnostic_repair_workflow"}, {"status", "repaired"}};
    };
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","diagnostics":[],"patch_candidates":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-workflow", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("diagnostic_repair_workflow"));
}

TEST(DiagnosticRepairApiTest, RepairWorkflowServiceUnavailableReturns500) {
    server::Router router;
    server::DiagnosticRepairApiService svc;
    server::register_diagnostic_repair_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"diagnostics":[],"patch_candidates":[]})";
    auto* handler = router.match("POST", "/api/diagnostics/repair-workflow", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

// ==================== Audit API ====================

TEST(AuditApiTest, EventsParsesFiltersAndUsername) {
    server::Router router;
    server::AuditApiService svc;
    svc.list_events = [](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         const container::String& category,
                         const container::String& action,
                         int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(category, container::String("permission"));
        EXPECT_EQ(action, container::String("requested"));
        EXPECT_EQ(limit, 25);
        auto events = ben_gear::Json::array();
        events.push_back(ben_gear::Json{{"event_id", "evt-1"}, {"category", "permission"}, {"action", "requested"}});
        return ben_gear::Json{{"success", true}, {"events", events}};
    };
    server::register_audit_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("session_id")] = container::String("sid-1");
    req.query[container::String("category")] = container::String("permission");
    req.query[container::String("action")] = container::String("requested");
    req.query[container::String("limit")] = container::String("25");
    auto* handler = router.match("GET", "/api/audit/events", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("evt-1"));
}

TEST(AuditApiTest, EventsDefaultsLimit) {
    server::Router router;
    server::AuditApiService svc;
    svc.list_events = [](const container::String&,
                         const container::String&,
                         const container::String&,
                         const container::String&,
                         const container::String&,
                         int limit) {
        EXPECT_EQ(limit, 100);
        return ben_gear::Json{{"success", true}, {"events", ben_gear::Json::array()}};
    };
    server::register_audit_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("limit")] = container::String("bad");
    auto* handler = router.match("GET", "/api/audit/events", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(AuditApiTest, EventsServiceUnavailableReturns500) {
    server::Router router;
    server::AuditApiService svc;
    server::register_audit_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/audit/events", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

// ==================== Patch API ====================

TEST(PatchApiTest, ListChangesRequiresSessionId) {
    server::Router router;
    server::PatchApiService svc;
    svc.list_changes = [](const container::String&, const container::String&, const container::String&) {
        return ben_gear::Json{{"success", true}, {"changes", ben_gear::Json::array()}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    auto* handler = router.match("GET", "/api/changes", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_THAT(resp.body, testing::HasSubstr("missing session_id"));
}

TEST(PatchApiTest, ReadChangeMapsNotFoundTo404) {
    server::Router router;
    server::PatchApiService svc;
    svc.read_change = [](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         std::string_view change_id) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(change_id, std::string_view("missing"));
        return ben_gear::Json{{"success", false}, {"error_type", "change_not_found"}, {"message", "not found"}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("default");
    req.query[container::String("session_id")] = container::String("sid-1");
    auto* handler = router.match("GET", "/api/changes/missing", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 404);
    EXPECT_THAT(resp.body, testing::HasSubstr("change_not_found"));
}

TEST(PatchApiTest, PreviewApplyAndRevertParseBody) {
    server::Router router;
    server::PatchApiService svc;
    svc.preview_patch = [](const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view unified_diff) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(unified_diff, std::string_view("diff-text"));
        return ben_gear::Json{{"success", true}, {"can_apply", true}};
    };
    svc.apply_patch = [](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         std::string_view unified_diff,
                         std::string_view description) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(unified_diff, std::string_view("diff-text"));
        EXPECT_EQ(description, std::string_view("desc"));
        return ben_gear::Json{{"success", true}, {"change_id", "chg_1"}};
    };
    svc.revert_change = [](const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view change_id,
                           bool force) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(change_id, std::string_view("chg_1"));
        EXPECT_TRUE(force);
        return ben_gear::Json{{"success", true}, {"change_id", "chg_1"}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest preview_req;
    preview_req.username = container::String("alice");
    preview_req.body = R"({"workspace":"default","session_id":"sid-1","unified_diff":"diff-text"})";
    auto* preview = router.match("POST", "/api/patch/preview", preview_req);
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ((*preview)(preview_req).status, 200);

    server::HttpRequest apply_req;
    apply_req.username = container::String("alice");
    apply_req.body = R"({"workspace":"default","session_id":"sid-1","unified_diff":"diff-text","description":"desc"})";
    auto* apply = router.match("POST", "/api/patch/apply", apply_req);
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ((*apply)(apply_req).status, 200);

    server::HttpRequest revert_req;
    revert_req.username = container::String("alice");
    revert_req.body = R"({"workspace":"default","session_id":"sid-1","force":true})";
    auto* revert = router.match("POST", "/api/changes/chg_1/revert", revert_req);
    ASSERT_NE(revert, nullptr);
    EXPECT_EQ((*revert)(revert_req).status, 200);
}

TEST(PatchApiTest, MutationRoutesDelegateGovernanceToApplicationService) {
    server::Router router;
    server::PatchApiService svc;
    bool apply_called = false;
    bool revert_called = false;
    svc.apply_patch = [&apply_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view) {
        apply_called = true;
        return ben_gear::Json{{"success", true}, {"change_id", "chg_1"}};
    };
    svc.revert_change = [&revert_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        revert_called = true;
        return ben_gear::Json{{"success", true}, {"change_id", "chg_1"}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest apply_req;
    apply_req.username = container::String("alice");
    apply_req.body = R"({"workspace":"default","session_id":"sid-1","unified_diff":"diff-text"})";
    auto* apply = router.match("POST", "/api/patch/apply", apply_req);
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ((*apply)(apply_req).status, 200);

    server::HttpRequest revert_req;
    revert_req.username = container::String("alice");
    revert_req.body = R"({"workspace":"default","session_id":"sid-1"})";
    auto* revert = router.match("POST", "/api/changes/chg_1/revert", revert_req);
    ASSERT_NE(revert, nullptr);
    EXPECT_EQ((*revert)(revert_req).status, 200);

    EXPECT_TRUE(apply_called);
    EXPECT_TRUE(revert_called);
}

// ==================== Auth ====================

TEST(AuthTest, NoApiKeyRequiresUsername) {
    ben_gear::config::ServerSettings settings;
    std::string username;

    server::HttpRequest missing;
    EXPECT_FALSE(server::authenticate(missing, settings, username));

    server::HttpRequest from_query;
    from_query.query[container::String("username")] = container::String("alice");
    EXPECT_TRUE(server::authenticate(from_query, settings, username));
    EXPECT_EQ(username, "alice");

    server::HttpRequest from_header;
    from_header.headers[container::String("x-username")] = container::String("bob");
    EXPECT_TRUE(server::authenticate(from_header, settings, username));
    EXPECT_EQ(username, "bob");
}

TEST(AuthTest, ApiKeyRequiresMatchingBearerToken) {
    ben_gear::config::ServerSettings settings;
    settings.api_key = container::String("secret");
    std::string username;

    server::HttpRequest bad;
    bad.headers[container::String("authorization")] = container::String("Bearer wrong");
    EXPECT_FALSE(server::authenticate(bad, settings, username));

    server::HttpRequest good;
    good.headers[container::String("authorization")] = container::String("Bearer secret");
    good.headers[container::String("x-username")] = container::String("carol");
    EXPECT_TRUE(server::authenticate(good, settings, username));
    EXPECT_EQ(username, "carol");
}

// ==================== Workbench API ====================

TEST(WorkbenchApiTest, SnapshotParsesWorkspaceFromBodyAndStripsIt) {
    server::Router router;
    server::WorkbenchSnapshotApiService svc;
    svc.snapshot = [](const container::String& workspace,
                      const container::String& username,
                      const ben_gear::Json& request) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_FALSE(request.contains("workspace"));
        EXPECT_EQ(request.value("symbol", ""), "Router");
        return ben_gear::Json{{"success", true}, {"provider", "workbench"}};
    };
    server::register_workbench_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","symbol":"Router"})";
    auto* handler = router.match("POST", "/api/workbench/snapshot", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("workbench"));
}

TEST(WorkbenchApiTest, SnapshotPrefersWorkspaceQueryAndRejectsInvalidJson) {
    server::Router router;
    server::WorkbenchSnapshotApiService svc;
    int calls = 0;
    svc.snapshot = [&calls](const container::String& workspace,
                            const container::String&,
                            const ben_gear::Json&) {
        ++calls;
        EXPECT_EQ(workspace, container::String("query-workspace"));
        return ben_gear::Json{{"success", true}};
    };
    server::register_workbench_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.query[container::String("workspace")] = container::String("query-workspace");
    req.body = R"({"workspace":"body-workspace"})";
    auto* handler = router.match("POST", "/api/workbench/snapshot", req);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ((*handler)(req).status, 200);
    EXPECT_EQ(calls, 1);

    server::HttpRequest bad;
    bad.username = container::String("alice");
    bad.body = R"(["bad"] )";
    EXPECT_EQ((*handler)(bad).status, 400);
    EXPECT_EQ(calls, 1);
}

namespace {

void write_server_test_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

void run_server_test_cmd(const std::filesystem::path& cwd, const std::string& command) {
    int rc;
#ifdef _WIN32
    if (command.rfind("git ", 0) == 0) {
        rc = std::system(("git -C \"" + cwd.string() + "\" " + command.substr(4) + " 2>&1").c_str());
    } else {
        rc = std::system(("cd /d \"" + cwd.string() + "\" && " + command + " 2>&1").c_str());
    }
#else
    rc = std::system(("cd '" + cwd.string() + "' && " + command + " >/dev/null 2>&1").c_str());
#endif
    ASSERT_EQ(rc, 0);
}

} // namespace

TEST(WorkbenchCompositionTest, SnapshotCombinesRepoCodeIntelAndAuditWithSharedIndex) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_composition_test";
    std::filesystem::remove_all(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "include/app.hpp", "class App { public: void run(); };\n");
    write_server_test_file(project_dir / "src/app.cpp", "#include \"app.hpp\"\nvoid use() { App app; app.run(); }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"workspace", "default"},
                           {"path", "include/app.hpp"},
                           {"symbol", "App"},
                           {"query", "App"},
                           {"max_dependencies", 20},
                           {"audit_limit", 5},
                           {"verification_result", ben_gear::Json{{"success", false}, {"exit_code", 1}, {"timed_out", false}, {"command", "ctest --output-on-failure"}, {"cwd", "."}, {"elapsed_ms", 12}, {"output", "FAILED AppTest"}, {"diagnostics", ben_gear::Json::array({ben_gear::Json{{"path", "src/app.cpp"}, {"line", 2}, {"message", "failure"}}})}}}};
    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    EXPECT_EQ(snapshot.value("provider", ""), "workbench");
    EXPECT_TRUE(snapshot.contains("overview"));
    EXPECT_TRUE(snapshot.contains("path"));
    EXPECT_TRUE(snapshot.contains("document_symbols"));
    EXPECT_TRUE(snapshot.contains("workspace_symbols"));
    EXPECT_TRUE(snapshot.contains("definition"));
    EXPECT_TRUE(snapshot.contains("references"));
    EXPECT_TRUE(snapshot.contains("navigation_contexts"));
    EXPECT_TRUE(snapshot.contains("change_context"));
    EXPECT_TRUE(snapshot.contains("quality_context"));
    EXPECT_TRUE(snapshot.contains("action_context"));
    EXPECT_TRUE(snapshot.contains("dependency_context"));
    EXPECT_TRUE(snapshot.contains("symbol_context"));
    EXPECT_TRUE(snapshot.contains("impact_context"));
    EXPECT_TRUE(snapshot.contains("readiness_context"));
    EXPECT_TRUE(snapshot.contains("failure_context"));
    EXPECT_TRUE(snapshot.contains("gate_context"));
    EXPECT_TRUE(snapshot.contains("timeline_context"));
    EXPECT_TRUE(snapshot.contains("agent_context"));
    EXPECT_TRUE(snapshot.contains("handoff_package"));
    EXPECT_TRUE(snapshot.contains("audit"));
    EXPECT_TRUE(snapshot.contains("source_context"));
    EXPECT_TRUE(snapshot["index"].value("request_scoped", false));
    EXPECT_EQ(snapshot["source_context"].value("path", ""), "include/app.hpp");
    EXPECT_FALSE(snapshot["source_context"]["lines"].empty());
    EXPECT_EQ(snapshot["definition"].value("symbol", ""), "App");
    EXPECT_FALSE(snapshot["references"]["references"].empty());
    EXPECT_TRUE(snapshot["navigation_contexts"].value("success", false));
    EXPECT_FALSE(snapshot["navigation_contexts"]["definition"]["contexts"].empty());
    EXPECT_FALSE(snapshot["navigation_contexts"]["references"]["contexts"].empty());
    EXPECT_TRUE(snapshot["change_context"].value("success", false));
    EXPECT_TRUE(snapshot["change_context"].contains("git_status"));
    EXPECT_TRUE(snapshot["action_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["verification_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["verification_context"].contains("commands"));
    EXPECT_TRUE(snapshot["verification_context"]["last_run"].value("provided", false));
    EXPECT_EQ(snapshot["verification_context"]["last_run"].value("status", ""), "failed");
    EXPECT_TRUE(snapshot["failure_context"].value("success", false));
    EXPECT_TRUE(snapshot["failure_context"].value("failed", false));
    EXPECT_TRUE(snapshot["failure_context"].contains("actions"));
    EXPECT_TRUE(snapshot["gate_context"].value("success", false));
    EXPECT_EQ(snapshot["gate_context"].value("decision", ""), "blocked");
    EXPECT_FALSE(snapshot["gate_context"].value("handoff_allowed", true));
    EXPECT_TRUE(snapshot["handoff_package"].value("success", false));
    EXPECT_EQ(snapshot["handoff_package"].value("package_version", 0), 1);
    EXPECT_TRUE(snapshot["handoff_package"].contains("schema"));
    EXPECT_EQ(snapshot["handoff_package"]["schema"].value("name", ""), "workbench_handoff_package");
    EXPECT_TRUE(snapshot["handoff_package"].contains("truncation"));
    EXPECT_TRUE(snapshot["handoff_package"].contains("limits"));
    EXPECT_TRUE(snapshot["handoff_package"].contains("gate"));
    EXPECT_TRUE(snapshot["handoff_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["handoff_context"].contains("brief"));
    EXPECT_TRUE(snapshot["review_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["review_context"].contains("checklist"));
    EXPECT_TRUE(snapshot["symbol_context"].value("success", false));
    EXPECT_GT(snapshot["symbol_context"]["summary"].value("document_count", 0), 0);
    EXPECT_TRUE(snapshot["impact_context"].value("success", false));
    EXPECT_TRUE(snapshot["impact_context"].value("read_only", false));
    EXPECT_GE(snapshot["impact_context"].value("score", 0), 0);
    EXPECT_TRUE(snapshot["impact_context"].contains("recommended_focus"));
    EXPECT_TRUE(snapshot["readiness_context"].value("success", false));
    EXPECT_TRUE(snapshot["readiness_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["readiness_context"].contains("decision"));
    EXPECT_TRUE(snapshot["readiness_context"].contains("suggestions"));
    EXPECT_EQ(snapshot["readiness_context"].value("decision", ""), "no_go");
    EXPECT_TRUE(snapshot["timeline_context"].value("success", false));
    EXPECT_TRUE(snapshot["timeline_context"].value("read_only", false));
    EXPECT_GT(snapshot["timeline_context"].value("entry_count", 0), 0);
    EXPECT_TRUE(snapshot["timeline_context"].contains("next_step"));
    EXPECT_TRUE(snapshot["agent_context"].value("success", false));
    EXPECT_TRUE(snapshot["agent_context"].value("read_only", false));
    EXPECT_TRUE(snapshot["agent_context"].contains("objective"));
    EXPECT_TRUE(snapshot["agent_context"].contains("handoff_prompt"));
    EXPECT_FALSE(snapshot["agent_context"]["evidence"].empty());
    ASSERT_FALSE(snapshot["symbol_context"]["document"]["contexts"].empty());
    EXPECT_TRUE(snapshot["symbol_context"]["document"]["contexts"][0].contains("context"));
    EXPECT_TRUE(snapshot["dependency_context"].value("success", false));
    EXPECT_GT(snapshot["dependency_context"]["summary"].value("dependent_count", 0), 0);
    ASSERT_FALSE(snapshot["dependency_context"]["dependents"].empty());
    EXPECT_TRUE(snapshot["dependency_context"]["dependents"][0].contains("context"));

    std::filesystem::remove_all(root);
}


TEST(WorkbenchCompositionTest, SnapshotGateReviewsWhenVerificationMissing) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_gate_missing_test";
    std::filesystem::remove_all(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src/app.cpp", "int main() { return 0; }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), ben_gear::Json{{"path", "src/app.cpp"}, {"audit_limit", 0}});

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("gate_context"));
    EXPECT_EQ(snapshot["gate_context"].value("decision", ""), "review");
    EXPECT_EQ(snapshot["gate_context"].value("verification_status", ""), "missing");
    ASSERT_TRUE(snapshot.contains("handoff_package"));
    EXPECT_EQ(snapshot["handoff_package"]["schema"].value("version", 0), 1);

    std::filesystem::remove_all(root);
}

TEST(WorkbenchCompositionTest, SnapshotPassedVerificationFeedsGateAndPackage) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_gate_pass_test";
    std::filesystem::remove_all(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src/app.cpp", "int main() { return 0; }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "src/app.cpp"},
                           {"audit_limit", 0},
                           {"verification_result", ben_gear::Json{{"success", true}, {"exit_code", 0}, {"timed_out", false}, {"command", "ctest"}, {"cwd", "."}, {"elapsed_ms", 7}, {"output", "pass"}, {"diagnostics", ben_gear::Json::array()}}}};
    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    EXPECT_EQ(snapshot["verification_context"]["last_run"].value("status", ""), "passed");
    EXPECT_NE(snapshot["gate_context"].value("verification_status", ""), "missing");
    EXPECT_EQ(snapshot["gate_context"].value("verification_status", ""), "passed");
    EXPECT_EQ(snapshot["handoff_package"]["verification"]["last_run"].value("status", ""), "passed");

    std::filesystem::remove_all(root);
}


TEST(WorkbenchCompositionTest, SnapshotIncludesGitChangeContextForSelectedPath) {
    static int wbench_cc_counter = 0;
    auto root = std::filesystem::temp_directory_path() / ("bengear_wbench_cc_test_" + std::to_string(++wbench_cc_counter));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "file.txt", "hello\n");
    run_server_test_cmd(project_dir, "git init");
    run_server_test_cmd(project_dir, "git config user.email test@example.com");
    run_server_test_cmd(project_dir, "git config user.name Test");
    run_server_test_cmd(project_dir, "git config core.autocrlf false");
    run_server_test_cmd(project_dir, "git add file.txt");
    run_server_test_cmd(project_dir, "git commit -m init");
    write_server_test_file(project_dir / "file.txt", "hello\nchanged\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "file.txt"}, {"audit_limit", 0}, {"max_files", 20}};
    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("change_context"));
    EXPECT_TRUE(snapshot["change_context"]["git_status"].value("success", false));
    EXPECT_FALSE(snapshot["change_context"]["git_status"].value("clean", true));
    EXPECT_EQ(snapshot["change_context"]["selected_file"].value("path", ""), "file.txt");
    EXPECT_NE(snapshot["change_context"]["diff"].value("diff", "").find("changed"), std::string::npos);
    ASSERT_TRUE(snapshot.contains("action_context"));
    EXPECT_GT(snapshot["action_context"].value("action_count", 0), 0);
    EXPECT_EQ(snapshot["action_context"]["actions"][0].value("id", ""), "review-selected-diff");
    EXPECT_TRUE(snapshot["verification_context"].value("dirty", false));
    EXPECT_EQ(snapshot["verification_context"].value("changed_files", 0), 1);
    EXPECT_EQ(snapshot["handoff_context"].value("status", ""), "review_changes");
    EXPECT_EQ(snapshot["handoff_context"]["brief"].value("changed_files", 0), 1);
    EXPECT_EQ(snapshot["review_context"].value("status", ""), "needs_review");
    EXPECT_GT(snapshot["review_context"].value("blocker_count", 0), 0);

    std::filesystem::remove_all(root, ec);
}


TEST(WorkbenchCompositionTest, SnapshotBuildsQualityContextFromDiagnostics) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_quality_context_test";
    std::filesystem::remove_all(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src" / "foo.cpp", "int main() {\n  return broken;\n}\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json diagnostic{{"path", "src/foo.cpp"}, {"line", 2}, {"column", 10}, {"severity", "error"}, {"message", "unknown identifier"}};
    ben_gear::Json request{{"path", "src/foo.cpp"}, {"diagnostics", ben_gear::Json::array({diagnostic})}, {"audit_limit", 0}, {"context_lines", 1}};
    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("quality_context"));
    auto quality = snapshot["quality_context"];
    EXPECT_TRUE(quality.value("success", false));
    EXPECT_TRUE(quality["diagnostic_context"].value("success", false));
    EXPECT_EQ(quality["diagnostic_context"].value("diagnostic_count", 0), 1);
    ASSERT_FALSE(quality["diagnostic_context"]["contexts"].empty());
    EXPECT_EQ(quality["diagnostic_context"]["contexts"][0]["snippet"].value("path", ""), "src/foo.cpp");
    ASSERT_TRUE(snapshot.contains("action_context"));
    EXPECT_GT(snapshot["action_context"].value("action_count", 0), 0);
    EXPECT_EQ(snapshot["action_context"]["actions"][0].value("id", ""), "inspect-diagnostics");
    EXPECT_TRUE(snapshot["verification_context"].value("diagnostics_provided", false));
    EXPECT_EQ(snapshot["verification_context"].value("diagnostic_count", 0), 1);
    ASSERT_FALSE(snapshot["verification_context"]["next_steps"].empty());
    EXPECT_EQ(snapshot["handoff_context"].value("status", ""), "diagnostics");
    EXPECT_EQ(snapshot["handoff_context"]["brief"].value("diagnostic_count", 0), 1);
    ASSERT_FALSE(snapshot["handoff_context"]["risks"].empty());
    EXPECT_EQ(snapshot["review_context"].value("status", ""), "needs_review");
    EXPECT_GT(snapshot["review_context"].value("blocker_count", 0), 0);
    ASSERT_FALSE(snapshot["review_context"]["focus"].empty());

    std::filesystem::remove_all(root);
}

TEST(WorkbenchCompositionTest, SnapshotRejectsSourceContextWorkspaceEscape) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_escape_test";
    std::filesystem::remove_all(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "main.cpp", "int main() { return 0; }\n");
    write_server_test_file(root / "secret.txt", "nope\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = container::String("default");
    config.fallback_project_path = container::String(project_dir.string().c_str());
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "../secret.txt"}, {"audit_limit", 0}};
    auto snapshot = svc.snapshot(container::String("default"), container::String("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("source_context"));
    EXPECT_FALSE(snapshot["source_context"].value("success", true));
    EXPECT_EQ(snapshot["source_context"].value("error_type", ""), "workspace_escape");

    std::filesystem::remove_all(root);
}


TEST(RuntimeApiTest, ListsReadsAndReturnsExecutionTrace) {
    server::Router router;
    server::RuntimeApiService svc;
    svc.list_executions = [](const container::String& workspace,
                             const container::String& session_id,
                             const container::String& username,
                             const container::String& action,
                             const container::String& status,
                             const container::String& capability,
                             int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(action, container::String("patch.apply"));
        EXPECT_EQ(status, container::String("succeeded"));
        EXPECT_EQ(capability, container::String("patch_apply"));
        EXPECT_EQ(limit, 7);
        return ben_gear::Json{{"success", true},
                              {"executions", ben_gear::Json::array({ben_gear::Json{{"execution_id", "exec-1"}}})}};
    };
    svc.read_execution = [](const container::String& username, const container::String& execution_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(execution_id, container::String("exec-1"));
        auto trace_entry = ben_gear::Json::object();
        trace_entry["step_id"] = "validate";
        auto execution_result = ben_gear::Json::object();
        execution_result["trace"] = ben_gear::Json::array({trace_entry});
        auto execution = ben_gear::Json::object();
        execution["execution_id"] = "exec-1";
        execution["execution"] = execution_result;
        return ben_gear::Json{{"success", true}, {"execution", execution}};
    };
    svc.list_links = [](const container::String& workspace,
                        const container::String& session_id,
                        const container::String& username,
                        const container::String& execution_id,
                        const container::String& relation,
                        int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(execution_id, container::String("exec-1"));
        EXPECT_EQ(relation, container::String("repair_patch"));
        EXPECT_EQ(limit, 3);
        return ben_gear::Json{{"success", true}, {"links", ben_gear::Json::array({ben_gear::Json{{"link_id", "link-1"}}})}};
    };
    svc.append_link = [](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         const container::String& source_execution_id,
                         const ben_gear::Json& body) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(source_execution_id, container::String("exec-1"));
        EXPECT_EQ(body.value("relation", ""), "repair_patch");
        return ben_gear::Json{{"success", true}, {"link", ben_gear::Json{{"link_id", "link-2"}}}};
    };
    server::register_runtime_routes(router, svc);

    server::HttpRequest list_req;
    list_req.username = container::String("alice");
    list_req.query[container::String("workspace")] = container::String("default");
    list_req.query[container::String("session_id")] = container::String("sid-1");
    list_req.query[container::String("action")] = container::String("patch.apply");
    list_req.query[container::String("status")] = container::String("succeeded");
    list_req.query[container::String("capability")] = container::String("patch_apply");
    list_req.query[container::String("limit")] = container::String("7");
    auto* list_handler = router.match(container::String("GET"), container::String("/api/runtime/executions"), list_req);
    ASSERT_NE(list_handler, nullptr);
    auto list_resp = (*list_handler)(list_req);
    EXPECT_EQ(list_resp.status, 200);
    auto list_body = ben_gear::Json::parse(list_resp.body);
    ASSERT_TRUE(list_body.value("success", false));
    ASSERT_EQ(list_body["executions"].size(), 1u);

    server::HttpRequest trace_req;
    trace_req.username = container::String("alice");
    auto* trace_handler = router.match(container::String("GET"), container::String("/api/runtime/executions/exec-1/trace"), trace_req);
    ASSERT_NE(trace_handler, nullptr);
    auto trace_resp = (*trace_handler)(trace_req);
    EXPECT_EQ(trace_resp.status, 200);
    auto trace_body = ben_gear::Json::parse(trace_resp.body);
    ASSERT_TRUE(trace_body.value("success", false));
    ASSERT_EQ(trace_body["trace"].size(), 1u);
    EXPECT_EQ(trace_body["trace"][0].value("step_id", ""), "validate");

    server::HttpRequest links_req;
    links_req.username = container::String("alice");
    links_req.query[container::String("workspace")] = container::String("default");
    links_req.query[container::String("session_id")] = container::String("sid-1");
    links_req.query[container::String("relation")] = container::String("repair_patch");
    links_req.query[container::String("limit")] = container::String("3");
    auto* links_handler = router.match(container::String("GET"), container::String("/api/runtime/executions/exec-1/links"), links_req);
    ASSERT_NE(links_handler, nullptr);
    auto links_resp = (*links_handler)(links_req);
    EXPECT_EQ(links_resp.status, 200);
    auto links_body = ben_gear::Json::parse(links_resp.body);
    ASSERT_TRUE(links_body.value("success", false));
    ASSERT_EQ(links_body["links"].size(), 1u);

    server::HttpRequest append_link_req;
    append_link_req.username = container::String("alice");
    append_link_req.query[container::String("workspace")] = container::String("default");
    append_link_req.query[container::String("session_id")] = container::String("sid-1");
    append_link_req.body = ben_gear::Json{{"relation", "repair_patch"}, {"target_execution_id", "exec-patch"}}.dump().to_std_string();
    auto* append_link_handler = router.match(container::String("POST"), container::String("/api/runtime/executions/exec-1/links"), append_link_req);
    ASSERT_NE(append_link_handler, nullptr);
    auto append_link_resp = (*append_link_handler)(append_link_req);
    EXPECT_EQ(append_link_resp.status, 200);
    auto append_link_body = ben_gear::Json::parse(append_link_resp.body);
    ASSERT_TRUE(append_link_body.value("success", false));
    EXPECT_EQ(append_link_body["link"].value("link_id", ""), "link-2");

}

TEST(RuntimeApiTest, WorkflowRoutesParseRequestsAndDelegateToService) {
    server::Router router;
    server::RuntimeApiService svc;
    svc.list_workflows = [](const container::String& workspace,
                            const container::String& session_id,
                            const container::String& username,
                            const container::String& status,
                            const container::String& source_execution_id,
                            int limit) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(status, container::String("paused"));
        EXPECT_EQ(source_execution_id, container::String("exec-1"));
        EXPECT_EQ(limit, 4);
        return ben_gear::Json{{"success", true}, {"workflows", ben_gear::Json::array({ben_gear::Json{{"workflow_id", "wf-1"}}})}};
    };
    svc.read_workflow = [](const container::String& username, const container::String& workflow_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(workflow_id, container::String("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}}}};
    };
    svc.start_repair_workflow = [](const container::String& workspace,
                                   const container::String& session_id,
                                   const container::String& username,
                                   const ben_gear::Json& body) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(body.value("source_execution_id", ""), "exec-1");
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}, {"status", "paused"}}}};
    };
    svc.resume_workflow = [](const container::String& username,
                             const container::String& workflow_id,
                             const ben_gear::Json& body) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(workflow_id, container::String("wf-1"));
        EXPECT_EQ(body.value("unified_diff", ""), "diff");
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-2"}, {"status", "running"}}}};
    };
    svc.cancel_workflow = [](const container::String& username, const container::String& workflow_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(workflow_id, container::String("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}, {"status", "cancelled"}}}};
    };
    svc.workflow_timeline = [](const container::String& username, const container::String& workflow_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(workflow_id, container::String("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow_id", "wf-1"}, {"nodes", ben_gear::Json::array()}, {"edges", ben_gear::Json::array()}, {"actions", ben_gear::Json::array({"resume"})}};
    };
    svc.workflow_integrity = [](const container::String& username, const container::String& workflow_id) {
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(workflow_id, container::String("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow_id", "wf-1"}, {"checks", ben_gear::Json::array()}, {"warnings", ben_gear::Json::array()}, {"errors", ben_gear::Json::array()}};
    };
    svc.compact_workflows = [](const container::String& username) {
        EXPECT_EQ(username, container::String("alice"));
        return ben_gear::Json{{"success", true}, {"compacted", 1}};
    };
    server::register_runtime_routes(router, svc);

    server::HttpRequest list_req;
    list_req.username = container::String("alice");
    list_req.query[container::String("workspace")] = container::String("default");
    list_req.query[container::String("session_id")] = container::String("sid-1");
    list_req.query[container::String("status")] = container::String("paused");
    list_req.query[container::String("source_execution_id")] = container::String("exec-1");
    list_req.query[container::String("limit")] = container::String("4");
    auto* list_handler = router.match(container::String("GET"), container::String("/api/runtime/workflows"), list_req);
    ASSERT_NE(list_handler, nullptr);
    EXPECT_EQ((*list_handler)(list_req).status, 200);

    server::HttpRequest read_req;
    read_req.username = container::String("alice");
    auto* read_handler = router.match(container::String("GET"), container::String("/api/runtime/workflows/wf-1"), read_req);
    ASSERT_NE(read_handler, nullptr);
    EXPECT_EQ((*read_handler)(read_req).status, 200);

    server::HttpRequest start_req;
    start_req.username = container::String("alice");
    start_req.query[container::String("workspace")] = container::String("default");
    start_req.query[container::String("session_id")] = container::String("sid-1");
    start_req.body = ben_gear::Json{{"source_execution_id", "exec-1"}}.dump().to_std_string();
    auto* start_handler = router.match(container::String("POST"), container::String("/api/runtime/workflows/repair"), start_req);
    ASSERT_NE(start_handler, nullptr);
    EXPECT_EQ((*start_handler)(start_req).status, 200);

    server::HttpRequest resume_req;
    resume_req.username = container::String("alice");
    resume_req.body = ben_gear::Json{{"unified_diff", "diff"}}.dump().to_std_string();
    auto* resume_handler = router.match(container::String("POST"), container::String("/api/runtime/workflows/wf-1/resume"), resume_req);
    ASSERT_NE(resume_handler, nullptr);
    EXPECT_EQ((*resume_handler)(resume_req).status, 200);

    server::HttpRequest cancel_req;
    cancel_req.username = container::String("alice");
    auto* cancel_handler = router.match(container::String("POST"), container::String("/api/runtime/workflows/wf-1/cancel"), cancel_req);
    ASSERT_NE(cancel_handler, nullptr);
    EXPECT_EQ((*cancel_handler)(cancel_req).status, 200);

    server::HttpRequest timeline_req;
    timeline_req.username = container::String("alice");
    auto* timeline_handler = router.match(container::String("GET"), container::String("/api/runtime/workflows/wf-1/timeline"), timeline_req);
    ASSERT_NE(timeline_handler, nullptr);
    EXPECT_EQ((*timeline_handler)(timeline_req).status, 200);

    server::HttpRequest integrity_req;
    integrity_req.username = container::String("alice");
    auto* integrity_handler = router.match(container::String("GET"), container::String("/api/runtime/workflows/wf-1/integrity"), integrity_req);
    ASSERT_NE(integrity_handler, nullptr);
    EXPECT_EQ((*integrity_handler)(integrity_req).status, 200);

    server::HttpRequest compact_req;
    compact_req.username = container::String("alice");
    auto* compact_handler = router.match(container::String("POST"), container::String("/api/runtime/workflows/compact"), compact_req);
    ASSERT_NE(compact_handler, nullptr);
    EXPECT_EQ((*compact_handler)(compact_req).status, 200);

}
