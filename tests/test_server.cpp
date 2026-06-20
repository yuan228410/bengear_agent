#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/llm/run_outcome.hpp"
#include "ben_gear/server/auth/auth.hpp"
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/server/api/git_api.hpp"
#include "ben_gear/server/api/permission_api.hpp"
#include "ben_gear/server/api/patch_api.hpp"

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

TEST(GitApiTest, CreateBranchParsesBodyAndChecksPermission) {
    server::Router router;
    server::GitApiService svc;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("git_branch"));
        EXPECT_EQ(arguments.value("action", ""), "create");
        EXPECT_EQ(arguments.value("name", ""), "feature/test");
        EXPECT_EQ(arguments.value("start_point", ""), "master");
        EXPECT_FALSE(arguments.value("force", true));
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

TEST(GitApiTest, CreateBranchPermissionRequiredDoesNotCallService) {
    server::Router router;
    server::GitApiService svc;
    bool create_called = false;
    svc.check_permission = [](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"policy_effect", "ask"}, {"permission_id", "perm_branch"}};
    };
    svc.create_branch = [&create_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view, bool) {
        create_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","name":"feature/test"})";
    auto* handler = router.match("POST", "/api/git/branches", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FALSE(create_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_branch"));
}

TEST(GitApiTest, SwitchBranchParsesBodyAndChecksPermission) {
    server::Router router;
    server::GitApiService svc;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("git_branch"));
        EXPECT_EQ(arguments.value("action", ""), "switch");
        EXPECT_EQ(arguments.value("name", ""), "feature/test");
        EXPECT_TRUE(arguments.value("force", false));
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

TEST(GitApiTest, BranchMutationAllowedPermissionCallsService) {
    server::Router router;
    server::GitApiService svc;
    int permission_checks = 0;
    bool create_called = false;
    bool switch_called = false;
    svc.check_permission = [&permission_checks](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        ++permission_checks;
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

    EXPECT_EQ(permission_checks, 2);
    EXPECT_TRUE(create_called);
    EXPECT_TRUE(switch_called);
}

TEST(GitApiTest, RestoreParsesBodyAndChecksPermission) {
    server::Router router;
    server::GitApiService svc;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("git_restore"));
        EXPECT_TRUE(arguments["paths"].is_array());
        EXPECT_EQ(arguments["paths"].size(), 1u);
        EXPECT_EQ(arguments["paths"][0].get<std::string>(), "src/main.cpp");
        EXPECT_FALSE(arguments.value("staged", true));
        EXPECT_TRUE(arguments.value("worktree", false));
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

TEST(GitApiTest, RestorePermissionRequiredDoesNotCallService) {
    server::Router router;
    server::GitApiService svc;
    bool restore_called = false;
    svc.check_permission = [](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"policy_effect", "ask"}, {"permission_id", "perm_restore"}};
    };
    svc.restore = [&restore_called](const container::String&, const container::String&, const container::String&, const std::vector<std::string>&, bool, bool) {
        restore_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/restore", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FALSE(restore_called);
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

TEST(GitApiTest, RestoreAllowedPermissionCallsService) {
    server::Router router;
    server::GitApiService svc;
    int permission_checks = 0;
    bool restore_called = false;
    svc.check_permission = [&permission_checks](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        ++permission_checks;
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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
    EXPECT_EQ(permission_checks, 1);
    EXPECT_TRUE(restore_called);
}

TEST(GitApiTest, CommitParsesBodyAndChecksPermission) {
    server::Router router;
    server::GitApiService svc;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("git_commit"));
        EXPECT_EQ(arguments.value("message", ""), "update file");
        EXPECT_TRUE(arguments["paths"].is_array());
        EXPECT_EQ(arguments["paths"].size(), 1u);
        EXPECT_EQ(arguments["paths"][0].get<std::string>(), "src/main.cpp");
        EXPECT_FALSE(arguments.value("all", true));
        EXPECT_FALSE(arguments.value("amend", true));
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

TEST(GitApiTest, CommitPermissionRequiredDoesNotCallService) {
    server::Router router;
    server::GitApiService svc;
    bool commit_called = false;
    svc.check_permission = [](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"policy_effect", "ask"}, {"permission_id", "perm_commit"}};
    };
    svc.commit = [&commit_called](const container::String&, const container::String&, const container::String&, std::string_view, const std::vector<std::string>&, bool, bool) {
        commit_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_git_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","message":"update file","paths":["src/main.cpp"]})";
    auto* handler = router.match("POST", "/api/git/commit", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FALSE(commit_called);
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
    bool permission_checked = false;
    bool commit_called = false;
    svc.check_permission = [&permission_checked](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        permission_checked = true;
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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
    EXPECT_FALSE(permission_checked);
    EXPECT_FALSE(commit_called);
}

TEST(GitApiTest, CommitAllowedPermissionCallsService) {
    server::Router router;
    server::GitApiService svc;
    int permission_checks = 0;
    bool commit_called = false;
    svc.check_permission = [&permission_checks](const container::String&, const container::String&, const container::String&, std::string_view, const ben_gear::Json&) {
        ++permission_checks;
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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
    EXPECT_EQ(permission_checks, 1);
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

TEST(PatchApiTest, ApplyPermissionRequiredDoesNotCallApply) {
    server::Router router;
    server::PatchApiService svc;
    bool apply_called = false;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("apply_patch"));
        EXPECT_EQ(arguments.value("unified_diff", ""), "diff-text");
        EXPECT_EQ(arguments.value("description", ""), "desc");
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"policy_effect", "ask"}, {"permission_id", "perm_1"}};
    };
    svc.apply_patch = [&apply_called](const container::String&, const container::String&, const container::String&, std::string_view, std::string_view) {
        apply_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","unified_diff":"diff-text","description":"desc"})";
    auto* handler = router.match("POST", "/api/patch/apply", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FALSE(apply_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_1"));
}

TEST(PatchApiTest, RevertPermissionRequiredDoesNotCallRevert) {
    server::Router router;
    server::PatchApiService svc;
    bool revert_called = false;
    svc.check_permission = [](const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              std::string_view tool_name,
                              const ben_gear::Json& arguments) {
        EXPECT_EQ(workspace, container::String("default"));
        EXPECT_EQ(session_id, container::String("sid-1"));
        EXPECT_EQ(username, container::String("alice"));
        EXPECT_EQ(tool_name, std::string_view("revert_patch"));
        EXPECT_EQ(arguments.value("change_id", ""), "chg_1");
        EXPECT_TRUE(arguments.value("force", false));
        return ben_gear::Json{{"success", false}, {"error_type", "permission_required"}, {"policy_effect", "ask"}, {"permission_id", "perm_2"}};
    };
    svc.revert_change = [&revert_called](const container::String&, const container::String&, const container::String&, std::string_view, bool) {
        revert_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_patch_routes(router, svc);

    server::HttpRequest req;
    req.username = container::String("alice");
    req.body = R"({"workspace":"default","session_id":"sid-1","force":true})";
    auto* handler = router.match("POST", "/api/changes/chg_1/revert", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FALSE(revert_called);
    EXPECT_THAT(resp.body, testing::HasSubstr("permission_required"));
    EXPECT_THAT(resp.body, testing::HasSubstr("perm_2"));
}

TEST(PatchApiTest, AllowedPermissionCallsMutationServices) {
    server::Router router;
    server::PatchApiService svc;
    int permission_checks = 0;
    bool apply_called = false;
    bool revert_called = false;
    svc.check_permission = [&permission_checks](const container::String&,
                                                const container::String&,
                                                const container::String&,
                                                std::string_view,
                                                const ben_gear::Json&) {
        ++permission_checks;
        return ben_gear::Json{{"success", true}, {"policy_effect", "allow"}};
    };
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

    EXPECT_EQ(permission_checks, 2);
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
