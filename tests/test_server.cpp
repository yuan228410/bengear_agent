#include "test_framework.hpp"

#include "llm/run_outcome.hpp"
#include "server/auth/auth.hpp"
#include "server/core/router.hpp"
#include "server/ws/protocol.hpp"
#include "server/api/repo_map_api.hpp"
#include "server/api/code_intel_api.hpp"
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
        10, 4, 20, 12, 3, 5, std::string("Total tool call limit reached"));

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
    auto retryable = llm::RunOutcome::provider_error(429, std::string("rate limited"));
    EXPECT_TRUE(retryable.retry.available);
    EXPECT_EQ(retryable.retry.mode, llm::RetryMode::retry_same);
    EXPECT_EQ(retryable.retry.after_seconds, 10);
    EXPECT_THAT(retryable.details_json, testing::HasSubstr("\"http_status\":429"));

    auto fatal = llm::RunOutcome::provider_error(400, std::string("bad request"));
    EXPECT_FALSE(fatal.retry.available);
    EXPECT_EQ(fatal.retry.mode, llm::RetryMode::none);
}

TEST(RunOutcomeTest, JsonEscapesMessageAndIncludesDetails) {
    auto out = llm::RunOutcome::internal_error(std::string("bad \"json\"\nline"));
    auto json = llm::to_json(out);

    EXPECT_THAT(json, testing::HasSubstr("\"reason\":\"internal_error\""));
    EXPECT_THAT(json, testing::HasSubstr("bad \\\"json\\\"\\nline"));
    EXPECT_THAT(json, testing::HasSubstr("\"retry\":"));
}

// ==================== WebSocket Protocol ====================

TEST(WsProtocolTest, ChatRoundTripKeepsWorkspaceAndPrompt) {
    auto msg = server::WsMessage::chat(std::string("sid-1"), std::string("hello"));
    msg.strings[std::string("workspace")] = std::string("default");

    auto parsed = server::WsMessage::from_json(msg.to_json());
    EXPECT_EQ(parsed.version, 1);
    EXPECT_EQ(parsed.type, std::string("chat"));
    EXPECT_EQ(parsed.session_id, std::string("sid-1"));
    EXPECT_EQ(parsed.strings[std::string("workspace")], std::string("default"));
    EXPECT_EQ(parsed.strings[std::string("prompt")], std::string("hello"));
}

TEST(WsProtocolTest, DoneWithOutcomeMergesUsageAndOutcome) {
    auto msg = server::WsMessage::done_with_outcome(
        std::string("sid-2"),
        R"({"prompt_tokens":12,"context_length":200})",
        llm::to_json(llm::RunOutcome::timeout(std::string("slow"))),
        1.25,
        0.5);
    msg.strings[std::string("workspace")] = std::string("ws-a");

    auto json = msg.to_json();
    EXPECT_THAT(json, testing::HasSubstr("\"type\":\"done\""));
    EXPECT_THAT(json, testing::HasSubstr("\"workspace\":\"ws-a\""));
    EXPECT_THAT(json, testing::HasSubstr("\"prompt_tokens\":12"));
    EXPECT_THAT(json, testing::HasSubstr("\"outcome\":"));
    EXPECT_THAT(json, testing::HasSubstr("\"reason\":\"timeout\""));
}

TEST(WsProtocolTest, TextDataIsEscapedAsJsonString) {
    auto msg = server::WsMessage::tool_result(
        std::string("sid-3"), std::string("read_file"), "plain \"text\"", 0.25);
    auto json = msg.to_json();

    EXPECT_THAT(json, testing::HasSubstr("\"data\":\"plain \\\"text\\\"\""));
    EXPECT_THAT(json, testing::HasSubstr("\"elapsed\":0.250"));
}

TEST(WsProtocolTest, PlanApplyDecisionKeepsStructuredData) {
    auto msg = server::WsMessage::plan_apply_decision(
        std::string("sid-4"),
        R"({"revision":7,"item_id":"step_1","decision_id":"decision_1","choice_id":"choice_1"})");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, std::string("plan_apply_decision"));
    EXPECT_EQ(parsed.session_id, std::string("sid-4"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"decision_id\":\"decision_1\""));
}

TEST(WsProtocolTest, PermissionStateKeepsStructuredData) {
    auto msg = server::WsMessage::permission_state(
        std::string("sid-5"),
        R"({"success":true,"permissions":[{"permission_id":"perm_1"}]})");
    msg.strings[std::string("workspace")] = std::string("default");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, std::string("permission_state"));
    EXPECT_EQ(parsed.session_id, std::string("sid-5"));
    EXPECT_EQ(parsed.strings[std::string("workspace")], std::string("default"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"permission_id\":\"perm_1\""));
}

TEST(WsProtocolTest, PermissionApproveRoundTripKeepsData) {
    auto msg = server::WsMessage::permission_approve(
        std::string("sid-6"),
        R"({"permission_id":"perm_2","allow_session":true})");
    msg.strings[std::string("workspace")] = std::string("default");
    auto parsed = server::WsMessage::from_json(msg.to_json());

    EXPECT_EQ(parsed.type, std::string("permission_approve"));
    EXPECT_EQ(parsed.session_id, std::string("sid-6"));
    EXPECT_EQ(parsed.strings[std::string("workspace")], std::string("default"));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"permission_id\":\"perm_2\""));
    EXPECT_THAT(parsed.json_data, testing::HasSubstr("\"allow_session\":true"));
}

// ==================== Router ====================

TEST(RouterTest, MatchesPathParamsByMethod) {
    server::Router router;
    router.add_route("GET", "/api/sessions/:id", [](const server::HttpRequest& req) {
        return server::HttpResponse::ok(std::string("{\"id\":\"")
            + req.params.at(std::string("id")).c_str() + "\"}");
    });

    server::HttpRequest req;
    auto* handler = router.match("GET", "/api/sessions/abc-123", req);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(req.params[std::string("id")], std::string("abc-123"));
    EXPECT_EQ((*handler)(req).body, std::string("{\"id\":\"abc-123\"}"));

    server::HttpRequest wrong_method;
    EXPECT_EQ(router.match("POST", "/api/sessions/abc-123", wrong_method), nullptr);
}

TEST(RouterTest, CorsAllowsConfiguredOrigin) {
    server::Router router;
    std::vector<std::string> origins;
    origins.push_back(std::string("https://app.test"));
    router.set_cors_origins(origins);

    server::HttpRequest req;
    req.headers[std::string("origin")] = std::string("https://app.test");
    auto resp = server::HttpResponse::ok();
    router.apply_cors(req, resp);

    EXPECT_EQ(resp.headers[std::string("Access-Control-Allow-Origin")], std::string("https://app.test"));
    EXPECT_EQ(resp.headers[std::string("Access-Control-Allow-Methods")], std::string("GET, POST, PUT, DELETE, OPTIONS"));
}

// ==================== Repo Map API ====================

TEST(RepoMapApiTest, OverviewParsesWorkspaceAndUsername) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.overview = [](const std::string& workspace,
                      const std::string& username) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        return ben_gear::Json{{"success", true}, {"summary", ben_gear::Json{{"project_root", "/repo"}}}, {"important_files", ben_gear::Json::array()}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    auto* handler = router.match("GET", "/api/repo-map/overview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("/repo"));
}

TEST(RepoMapApiTest, FindFilesParsesQueryFiltersAndLimit) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.find_files = [](const std::string& workspace,
                        const std::string& username,
                        std::string_view query,
                        std::string_view kind,
                        std::string_view language,
                        int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
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
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("query")] = std::string("server");
    req.query[std::string("kind")] = std::string("source");
    req.query[std::string("language")] = std::string("cpp");
    req.query[std::string("limit")] = std::string("12");
    auto* handler = router.match("GET", "/api/repo-map/files", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("src/server.cpp"));
}

TEST(RepoMapApiTest, FindSymbolsParsesQueryFiltersAndLimit) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.find_symbols = [](const std::string& workspace,
                          const std::string& username,
                          std::string_view query,
                          std::string_view kind,
                          std::string_view language,
                          int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
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
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("query")] = std::string("Router");
    req.query[std::string("kind")] = std::string("class");
    req.query[std::string("language")] = std::string("cpp");
    req.query[std::string("limit")] = std::string("8");
    auto* handler = router.match("GET", "/api/repo-map/symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("Router"));
}

TEST(RepoMapApiTest, ExplainParsesPath) {
    server::Router router;
    server::RepoMapApiService svc;
    svc.explain_path = [](const std::string& workspace,
                          const std::string& username,
                          std::string_view path) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(path, std::string_view("src/server.cpp"));
        return ben_gear::Json{{"success", true}, {"file", ben_gear::Json{{"path", std::string(path)}}}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("path")] = std::string("src/server.cpp");
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
    svc.explain_path = [&explain_called](const std::string&, const std::string&, std::string_view) {
        explain_called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_repo_map_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
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
    req.username = std::string("alice");
    auto* handler = router.match("GET", "/api/repo-map/overview", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}

// ==================== Code Intelligence API ====================

TEST(CodeIntelApiTest, CapabilitiesParsesWorkspaceAndUsername) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.capabilities = [](const std::string& workspace,
                          const std::string& username) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        return ben_gear::Json{{"success", true}, {"provider", "indexed"}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    auto* handler = router.match("GET", "/api/code-intel/capabilities", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("indexed"));
}

TEST(CodeIntelApiTest, DocumentSymbolsParsesPath) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.document_symbols = [](const std::string& workspace,
                              const std::string& username,
                              std::string_view path) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(path, std::string_view("src/server.cpp"));
        return ben_gear::Json{{"success", true}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("path")] = std::string("src/server.cpp");
    auto* handler = router.match("GET", "/api/code-intel/document-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, DocumentSymbolsRequiresPath) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.document_symbols = [&called](const std::string&, const std::string&, std::string_view) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    auto* handler = router.match("GET", "/api/code-intel/document-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(CodeIntelApiTest, WorkspaceSymbolsParsesFiltersAndLimit) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.workspace_symbols = [](const std::string& workspace,
                               const std::string& username,
                               std::string_view query,
                               std::string_view kind,
                               std::string_view language,
                               int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(query, std::string_view("Router"));
        EXPECT_EQ(kind, std::string_view("class"));
        EXPECT_EQ(language, std::string_view("cpp"));
        EXPECT_EQ(limit, 9);
        return ben_gear::Json{{"success", true}, {"symbols", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("query")] = std::string("Router");
    req.query[std::string("kind")] = std::string("class");
    req.query[std::string("language")] = std::string("cpp");
    req.query[std::string("limit")] = std::string("9");
    auto* handler = router.match("GET", "/api/code-intel/workspace-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, WorkspaceSymbolsAllowsEmptyQuery) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.workspace_symbols = [&called](const std::string&,
                                      const std::string&,
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
    req.username = std::string("alice");
    auto* handler = router.match("GET", "/api/code-intel/workspace-symbols", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(called);
}

TEST(CodeIntelApiTest, DefinitionAcceptsSymbol) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.definition = [](const std::string& workspace,
                        const std::string& username,
                        std::string_view path,
                        int line,
                        int column,
                        std::string_view symbol,
                        int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_TRUE(path.empty());
        EXPECT_EQ(line, 0);
        EXPECT_EQ(column, 0);
        EXPECT_EQ(symbol, std::string_view("Router"));
        EXPECT_EQ(limit, 7);
        return ben_gear::Json{{"success", true}, {"symbol", "Router"}, {"definitions", ben_gear::Json::array()}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
    req.query[std::string("symbol")] = std::string("Router");
    req.query[std::string("limit")] = std::string("7");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("Router"));
}

TEST(CodeIntelApiTest, DefinitionAcceptsPosition) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.definition = [](const std::string&,
                        const std::string&,
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
    req.username = std::string("alice");
    req.query[std::string("path")] = std::string("src/router.cpp");
    req.query[std::string("line")] = std::string("12");
    req.query[std::string("column")] = std::string("5");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
}

TEST(CodeIntelApiTest, DefinitionRequiresSymbolOrPosition) {
    server::Router router;
    server::CodeIntelApiService svc;
    bool called = false;
    svc.definition = [&called](const std::string&, const std::string&, std::string_view, int, int, std::string_view, int) {
        called = true;
        return ben_gear::Json{{"success", true}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("path")] = std::string("src/router.cpp");
    req.query[std::string("line")] = std::string("0");
    req.query[std::string("column")] = std::string("5");
    auto* handler = router.match("GET", "/api/code-intel/definition", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_FALSE(called);
}

TEST(CodeIntelApiTest, ReferencesParsesLimit) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.references = [](const std::string&,
                        const std::string&,
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
    req.username = std::string("alice");
    req.query[std::string("symbol")] = std::string("Router");
    req.query[std::string("limit")] = std::string("3");
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
    req.username = std::string("alice");
    auto* handler = router.match("GET", "/api/code-intel/capabilities", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 500);
}


TEST(CodeIntelApiTest, ContextPackParsesBody) {
    server::Router router;
    server::CodeIntelApiService svc;
    svc.context_pack = [](const std::string& workspace,
                          const std::string& username,
                          const ben_gear::Json& request) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_TRUE(request.contains("diagnostics"));
        return ben_gear::Json{{"success", true}, {"context_pack", ben_gear::Json{{"context_pack_id", "ctx-1"}}}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("default");
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
    svc.read_context_pack = [](const std::string& username,
                               std::string_view context_pack_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(context_pack_id, std::string_view("ctx-1"));
        return ben_gear::Json{{"success", true}, {"context_pack", ben_gear::Json{{"context_pack_id", "ctx-1"}}}};
    };
    server::register_code_intel_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    auto* handler = router.match("GET", "/api/code-intel/context-packs/ctx-1", req);
    ASSERT_NE(handler, nullptr);
    auto resp = (*handler)(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_THAT(resp.body, testing::HasSubstr("ctx-1"));
}

// ==================== Auth ====================

TEST(AuthTest, NoApiKeyRequiresUsername) {
    ben_gear::config::ServerSettings settings;
    std::string username;

    server::HttpRequest missing;
    EXPECT_FALSE(server::authenticate(missing, settings, username));

    server::HttpRequest from_query;
    from_query.query[std::string("username")] = std::string("alice");
    EXPECT_TRUE(server::authenticate(from_query, settings, username));
    EXPECT_EQ(username, "alice");

    server::HttpRequest from_header;
    from_header.headers[std::string("x-username")] = std::string("bob");
    EXPECT_TRUE(server::authenticate(from_header, settings, username));
    EXPECT_EQ(username, "bob");
}

TEST(AuthTest, ApiKeyRequiresMatchingBearerToken) {
    ben_gear::config::ServerSettings settings;
    settings.api_key = std::string("secret");
    std::string username;

    server::HttpRequest bad;
    bad.headers[std::string("authorization")] = std::string("Bearer wrong");
    EXPECT_FALSE(server::authenticate(bad, settings, username));

    server::HttpRequest good;
    good.headers[std::string("authorization")] = std::string("Bearer secret");
    good.headers[std::string("x-username")] = std::string("carol");
    EXPECT_TRUE(server::authenticate(good, settings, username));
    EXPECT_EQ(username, "carol");
}

// ==================== Workbench API ====================

TEST(WorkbenchApiTest, SnapshotParsesWorkspaceFromBodyAndStripsIt) {
    server::Router router;
    server::WorkbenchSnapshotApiService svc;
    svc.snapshot = [](const std::string& workspace,
                      const std::string& username,
                      const ben_gear::Json& request) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_FALSE(request.contains("workspace"));
        EXPECT_EQ(request.value("symbol", ""), "Router");
        return ben_gear::Json{{"success", true}, {"provider", "workbench"}};
    };
    server::register_workbench_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
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
    svc.snapshot = [&calls](const std::string& workspace,
                            const std::string&,
                            const ben_gear::Json&) {
        ++calls;
        EXPECT_EQ(workspace, std::string("query-workspace"));
        return ben_gear::Json{{"success", true}};
    };
    server::register_workbench_routes(router, svc);

    server::HttpRequest req;
    req.username = std::string("alice");
    req.query[std::string("workspace")] = std::string("query-workspace");
    req.body = R"({"workspace":"body-workspace"})";
    auto* handler = router.match("POST", "/api/workbench/snapshot", req);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ((*handler)(req).status, 200);
    EXPECT_EQ(calls, 1);

    server::HttpRequest bad;
    bad.username = std::string("alice");
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
    bengear::test::force_remove_dir(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "include/app.hpp", "class App { public: void run(); };\n");
    write_server_test_file(project_dir / "src/app.cpp", "#include \"app.hpp\"\nvoid use() { App app; app.run(); }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
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
    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), request);

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

    bengear::test::force_remove_dir(root);
}


TEST(WorkbenchCompositionTest, SnapshotGateReviewsWhenVerificationMissing) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_gate_missing_test";
    bengear::test::force_remove_dir(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src/app.cpp", "int main() { return 0; }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), ben_gear::Json{{"path", "src/app.cpp"}, {"audit_limit", 0}});

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("gate_context"));
    EXPECT_EQ(snapshot["gate_context"].value("decision", ""), "review");
    EXPECT_EQ(snapshot["gate_context"].value("verification_status", ""), "missing");
    ASSERT_TRUE(snapshot.contains("handoff_package"));
    EXPECT_EQ(snapshot["handoff_package"]["schema"].value("version", 0), 1);

    bengear::test::force_remove_dir(root);
}

TEST(WorkbenchCompositionTest, SnapshotPassedVerificationFeedsGateAndPackage) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_gate_pass_test";
    bengear::test::force_remove_dir(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src/app.cpp", "int main() { return 0; }\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "src/app.cpp"},
                           {"audit_limit", 0},
                           {"verification_result", ben_gear::Json{{"success", true}, {"exit_code", 0}, {"timed_out", false}, {"command", "ctest"}, {"cwd", "."}, {"elapsed_ms", 7}, {"output", "pass"}, {"diagnostics", ben_gear::Json::array()}}}};
    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    EXPECT_EQ(snapshot["verification_context"]["last_run"].value("status", ""), "passed");
    EXPECT_NE(snapshot["gate_context"].value("verification_status", ""), "missing");
    EXPECT_EQ(snapshot["gate_context"].value("verification_status", ""), "passed");
    EXPECT_EQ(snapshot["handoff_package"]["verification"]["last_run"].value("status", ""), "passed");

    bengear::test::force_remove_dir(root);
}


TEST(WorkbenchCompositionTest, SnapshotIncludesGitChangeContextForSelectedPath) {
    static int wbench_cc_counter = 0;
    auto root = std::filesystem::temp_directory_path() / ("bengear_wbench_cc_test_" + std::to_string(++wbench_cc_counter));
    bengear::test::force_remove_dir(root);
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
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "file.txt"}, {"audit_limit", 0}, {"max_files", 20}};
    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), request);

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

    bengear::test::force_remove_dir(root);
}


TEST(WorkbenchCompositionTest, SnapshotBuildsQualityContextFromDiagnostics) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_quality_context_test";
    bengear::test::force_remove_dir(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "src" / "foo.cpp", "int main() {\n  return broken;\n}\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json diagnostic{{"path", "src/foo.cpp"}, {"line", 2}, {"column", 10}, {"severity", "error"}, {"message", "unknown identifier"}};
    ben_gear::Json request{{"path", "src/foo.cpp"}, {"diagnostics", ben_gear::Json::array({diagnostic})}, {"audit_limit", 0}, {"context_lines", 1}};
    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), request);

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

    bengear::test::force_remove_dir(root);
}

TEST(WorkbenchCompositionTest, SnapshotRejectsSourceContextWorkspaceEscape) {
    auto root = std::filesystem::temp_directory_path() / "bengear_workbench_escape_test";
    bengear::test::force_remove_dir(root);
    auto user_dir = root / "user";
    auto project_dir = root / "project";
    write_server_test_file(project_dir / "main.cpp", "int main() { return 0; }\n");
    write_server_test_file(root / "secret.txt", "nope\n");

    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = user_dir;
    config.default_workspace = std::string("default");
    config.fallback_project_path = project_dir.string();
    ben_gear::application::WorkspaceResolver resolver(config);
    ben_gear::config::Settings settings;
    server::SessionPool pool;
    auto svc = server::composition::make_workbench_snapshot_api_service(
        server::composition::ServerCompositionContext{settings, resolver, pool});

    ben_gear::Json request{{"path", "../secret.txt"}, {"audit_limit", 0}};
    auto snapshot = svc.snapshot(std::string("default"), std::string("alice"), request);

    ASSERT_TRUE(snapshot.value("success", false));
    ASSERT_TRUE(snapshot.contains("source_context"));
    EXPECT_FALSE(snapshot["source_context"].value("success", true));
    EXPECT_EQ(snapshot["source_context"].value("error_type", ""), "workspace_escape");

    bengear::test::force_remove_dir(root);
}


TEST(RuntimeApiTest, ListsReadsAndReturnsExecutionTrace) {
    server::Router router;
    server::RuntimeApiService svc;
    svc.list_executions = [](const std::string& workspace,
                             const std::string& session_id,
                             const std::string& username,
                             const std::string& action,
                             const std::string& status,
                             const std::string& capability,
                             int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(session_id, std::string("sid-1"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(action, std::string("patch.apply"));
        EXPECT_EQ(status, std::string("succeeded"));
        EXPECT_EQ(capability, std::string("patch_apply"));
        EXPECT_EQ(limit, 7);
        return ben_gear::Json{{"success", true},
                              {"executions", ben_gear::Json::array({ben_gear::Json{{"execution_id", "exec-1"}}})}};
    };
    svc.read_execution = [](const std::string& username, const std::string& execution_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(execution_id, std::string("exec-1"));
        auto trace_entry = ben_gear::Json::object();
        trace_entry["step_id"] = "validate";
        auto execution_result = ben_gear::Json::object();
        execution_result["trace"] = ben_gear::Json::array({trace_entry});
        auto execution = ben_gear::Json::object();
        execution["execution_id"] = "exec-1";
        execution["execution"] = execution_result;
        return ben_gear::Json{{"success", true}, {"execution", execution}};
    };
    svc.list_links = [](const std::string& workspace,
                        const std::string& session_id,
                        const std::string& username,
                        const std::string& execution_id,
                        const std::string& relation,
                        int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(session_id, std::string("sid-1"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(execution_id, std::string("exec-1"));
        EXPECT_EQ(relation, std::string("repair_patch"));
        EXPECT_EQ(limit, 3);
        return ben_gear::Json{{"success", true}, {"links", ben_gear::Json::array({ben_gear::Json{{"link_id", "link-1"}}})}};
    };
    svc.append_link = [](const std::string& workspace,
                         const std::string& session_id,
                         const std::string& username,
                         const std::string& source_execution_id,
                         const ben_gear::Json& body) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(session_id, std::string("sid-1"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(source_execution_id, std::string("exec-1"));
        EXPECT_EQ(body.value("relation", ""), "repair_patch");
        return ben_gear::Json{{"success", true}, {"link", ben_gear::Json{{"link_id", "link-2"}}}};
    };
    server::register_runtime_routes(router, svc);

    server::HttpRequest list_req;
    list_req.username = std::string("alice");
    list_req.query[std::string("workspace")] = std::string("default");
    list_req.query[std::string("session_id")] = std::string("sid-1");
    list_req.query[std::string("action")] = std::string("patch.apply");
    list_req.query[std::string("status")] = std::string("succeeded");
    list_req.query[std::string("capability")] = std::string("patch_apply");
    list_req.query[std::string("limit")] = std::string("7");
    auto* list_handler = router.match(std::string("GET"), std::string("/api/runtime/executions"), list_req);
    ASSERT_NE(list_handler, nullptr);
    auto list_resp = (*list_handler)(list_req);
    EXPECT_EQ(list_resp.status, 200);
    auto list_body = ben_gear::Json::parse(list_resp.body);
    ASSERT_TRUE(list_body.value("success", false));
    ASSERT_EQ(list_body["executions"].size(), 1u);

    server::HttpRequest trace_req;
    trace_req.username = std::string("alice");
    auto* trace_handler = router.match(std::string("GET"), std::string("/api/runtime/executions/exec-1/trace"), trace_req);
    ASSERT_NE(trace_handler, nullptr);
    auto trace_resp = (*trace_handler)(trace_req);
    EXPECT_EQ(trace_resp.status, 200);
    auto trace_body = ben_gear::Json::parse(trace_resp.body);
    ASSERT_TRUE(trace_body.value("success", false));
    ASSERT_EQ(trace_body["trace"].size(), 1u);
    EXPECT_EQ(trace_body["trace"][0].value("step_id", ""), "validate");

    server::HttpRequest links_req;
    links_req.username = std::string("alice");
    links_req.query[std::string("workspace")] = std::string("default");
    links_req.query[std::string("session_id")] = std::string("sid-1");
    links_req.query[std::string("relation")] = std::string("repair_patch");
    links_req.query[std::string("limit")] = std::string("3");
    auto* links_handler = router.match(std::string("GET"), std::string("/api/runtime/executions/exec-1/links"), links_req);
    ASSERT_NE(links_handler, nullptr);
    auto links_resp = (*links_handler)(links_req);
    EXPECT_EQ(links_resp.status, 200);
    auto links_body = ben_gear::Json::parse(links_resp.body);
    ASSERT_TRUE(links_body.value("success", false));
    ASSERT_EQ(links_body["links"].size(), 1u);

    server::HttpRequest append_link_req;
    append_link_req.username = std::string("alice");
    append_link_req.query[std::string("workspace")] = std::string("default");
    append_link_req.query[std::string("session_id")] = std::string("sid-1");
    append_link_req.body = ben_gear::Json{{"relation", "repair_patch"}, {"target_execution_id", "exec-patch"}}.dump();
    auto* append_link_handler = router.match(std::string("POST"), std::string("/api/runtime/executions/exec-1/links"), append_link_req);
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
    svc.list_workflows = [](const std::string& workspace,
                            const std::string& session_id,
                            const std::string& username,
                            const std::string& status,
                            const std::string& source_execution_id,
                            int limit) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(session_id, std::string("sid-1"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(status, std::string("paused"));
        EXPECT_EQ(source_execution_id, std::string("exec-1"));
        EXPECT_EQ(limit, 4);
        return ben_gear::Json{{"success", true}, {"workflows", ben_gear::Json::array({ben_gear::Json{{"workflow_id", "wf-1"}}})}};
    };
    svc.read_workflow = [](const std::string& username, const std::string& workflow_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(workflow_id, std::string("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}}}};
    };
    svc.start_repair_workflow = [](const std::string& workspace,
                                   const std::string& session_id,
                                   const std::string& username,
                                   const ben_gear::Json& body) {
        EXPECT_EQ(workspace, std::string("default"));
        EXPECT_EQ(session_id, std::string("sid-1"));
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(body.value("source_execution_id", ""), "exec-1");
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}, {"status", "paused"}}}};
    };
    svc.resume_workflow = [](const std::string& username,
                             const std::string& workflow_id,
                             const ben_gear::Json& body) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(workflow_id, std::string("wf-1"));
        EXPECT_EQ(body.value("unified_diff", ""), "diff");
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-2"}, {"status", "running"}}}};
    };
    svc.cancel_workflow = [](const std::string& username, const std::string& workflow_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(workflow_id, std::string("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow", ben_gear::Json{{"workflow_id", "wf-1"}, {"status", "cancelled"}}}};
    };
    svc.workflow_timeline = [](const std::string& username, const std::string& workflow_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(workflow_id, std::string("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow_id", "wf-1"}, {"nodes", ben_gear::Json::array()}, {"edges", ben_gear::Json::array()}, {"actions", ben_gear::Json::array({"resume"})}};
    };
    svc.workflow_integrity = [](const std::string& username, const std::string& workflow_id) {
        EXPECT_EQ(username, std::string("alice"));
        EXPECT_EQ(workflow_id, std::string("wf-1"));
        return ben_gear::Json{{"success", true}, {"workflow_id", "wf-1"}, {"checks", ben_gear::Json::array()}, {"warnings", ben_gear::Json::array()}, {"errors", ben_gear::Json::array()}};
    };
    svc.compact_workflows = [](const std::string& username) {
        EXPECT_EQ(username, std::string("alice"));
        return ben_gear::Json{{"success", true}, {"compacted", 1}};
    };
    server::register_runtime_routes(router, svc);

    server::HttpRequest list_req;
    list_req.username = std::string("alice");
    list_req.query[std::string("workspace")] = std::string("default");
    list_req.query[std::string("session_id")] = std::string("sid-1");
    list_req.query[std::string("status")] = std::string("paused");
    list_req.query[std::string("source_execution_id")] = std::string("exec-1");
    list_req.query[std::string("limit")] = std::string("4");
    auto* list_handler = router.match(std::string("GET"), std::string("/api/runtime/workflows"), list_req);
    ASSERT_NE(list_handler, nullptr);
    EXPECT_EQ((*list_handler)(list_req).status, 200);

    server::HttpRequest read_req;
    read_req.username = std::string("alice");
    auto* read_handler = router.match(std::string("GET"), std::string("/api/runtime/workflows/wf-1"), read_req);
    ASSERT_NE(read_handler, nullptr);
    EXPECT_EQ((*read_handler)(read_req).status, 200);

    server::HttpRequest start_req;
    start_req.username = std::string("alice");
    start_req.query[std::string("workspace")] = std::string("default");
    start_req.query[std::string("session_id")] = std::string("sid-1");
    start_req.body = ben_gear::Json{{"source_execution_id", "exec-1"}}.dump();
    auto* start_handler = router.match(std::string("POST"), std::string("/api/runtime/workflows/repair"), start_req);
    ASSERT_NE(start_handler, nullptr);
    EXPECT_EQ((*start_handler)(start_req).status, 200);

    server::HttpRequest resume_req;
    resume_req.username = std::string("alice");
    resume_req.body = ben_gear::Json{{"unified_diff", "diff"}}.dump();
    auto* resume_handler = router.match(std::string("POST"), std::string("/api/runtime/workflows/wf-1/resume"), resume_req);
    ASSERT_NE(resume_handler, nullptr);
    EXPECT_EQ((*resume_handler)(resume_req).status, 200);

    server::HttpRequest cancel_req;
    cancel_req.username = std::string("alice");
    auto* cancel_handler = router.match(std::string("POST"), std::string("/api/runtime/workflows/wf-1/cancel"), cancel_req);
    ASSERT_NE(cancel_handler, nullptr);
    EXPECT_EQ((*cancel_handler)(cancel_req).status, 200);

    server::HttpRequest timeline_req;
    timeline_req.username = std::string("alice");
    auto* timeline_handler = router.match(std::string("GET"), std::string("/api/runtime/workflows/wf-1/timeline"), timeline_req);
    ASSERT_NE(timeline_handler, nullptr);
    EXPECT_EQ((*timeline_handler)(timeline_req).status, 200);

    server::HttpRequest integrity_req;
    integrity_req.username = std::string("alice");
    auto* integrity_handler = router.match(std::string("GET"), std::string("/api/runtime/workflows/wf-1/integrity"), integrity_req);
    ASSERT_NE(integrity_handler, nullptr);
    EXPECT_EQ((*integrity_handler)(integrity_req).status, 200);

    server::HttpRequest compact_req;
    compact_req.username = std::string("alice");
    auto* compact_handler = router.match(std::string("POST"), std::string("/api/runtime/workflows/compact"), compact_req);
    ASSERT_NE(compact_handler, nullptr);
    EXPECT_EQ((*compact_handler)(compact_req).status, 200);

}
