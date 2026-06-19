#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/llm/run_outcome.hpp"
#include "ben_gear/server/auth/auth.hpp"
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/server/api/patch_api.hpp"

#include <string>

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
