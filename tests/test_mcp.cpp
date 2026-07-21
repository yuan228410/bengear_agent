/**
 * @file test_mcp.cpp
 * @brief MCP 配置/客户端/管理器测试
 */
#include "test_framework.hpp"
#include "capabilities/mcp/mcp_config.hpp"
#include "capabilities/mcp/mcp_client.hpp"
#include "config/settings.hpp"
#include "net/tls/tls_engine.hpp"
#include <vector>

namespace mcp = ben_gear::mcp;
namespace cfg = ben_gear::config;

// ============================================================
// 配置相关测试
// ============================================================
TEST(mcp, get_enabled_servers_empty) {
    cfg::Settings settings;
    auto enabled = mcp::get_enabled_servers(settings);
    EXPECT_TRUE(enabled.empty());
}

TEST(mcp, get_enabled_servers_disabled) {
    cfg::Settings settings;
    settings.mcp_servers["srv_disabled"].disabled = true;
    auto enabled = mcp::get_enabled_servers(settings);
    EXPECT_TRUE(enabled.empty());
}

TEST(mcp, get_enabled_servers_one) {
    cfg::Settings settings;
    settings.mcp_servers["srv1"].disabled = false;
    auto enabled = mcp::get_enabled_servers(settings);
    EXPECT_EQ(enabled.size(), (size_t)1);
}

TEST(mcp, get_enabled_servers_mixed) {
    cfg::Settings settings;
    settings.mcp_servers["srv_enabled"].disabled = false;
    settings.mcp_servers["srv_disabled"].disabled = true;
    settings.mcp_servers["srv_enabled2"].disabled = false;
    auto enabled = mcp::get_enabled_servers(settings);
    EXPECT_EQ(enabled.size(), (size_t)2);
}

TEST(mcp, transport_type_http) {
    cfg::MCPServerConfig cfg;
    cfg.disabled = false;
    cfg.url = "http://localhost:8080";
    auto t = mcp::transport_type(cfg);
    EXPECT_STREQ(t.data(), "http");
}

TEST(mcp, transport_type_stdio) {
    cfg::MCPServerConfig cfg;
    cfg.disabled = false;
    cfg.url.clear();
    auto t = mcp::transport_type(cfg);
    EXPECT_STREQ(t.data(), "stdio");
}

// ============================================================
// MCPClient 构造与基本状态
// ============================================================
TEST(mcp, client_default_construct) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPClient client(4096, *tls);
    EXPECT_FALSE(client.is_connected());
}

TEST(mcp, client_custom_buffer) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPClient client(8192, *tls, 30000, nullptr);
    EXPECT_FALSE(client.is_connected());
}

TEST(mcp, client_disconnect_before_connect) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPClient client(4096, *tls);
    // disconnect() on unconnected client should be safe (no-op)
    client.disconnect();
    EXPECT_FALSE(client.is_connected());
}

TEST(mcp, client_server_name_unconnected) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPClient client(4096, *tls);
    auto& name = client.server_name();
    EXPECT_STREQ(name.data(), "");
}

// ============================================================
// MCPManager 构造与基本状态
// ============================================================
TEST(mcp, manager_default_empty) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPManager mgr(4096, *tls);
    EXPECT_TRUE(mgr.empty());
}

TEST(mcp, manager_disconnect_all_empty) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPManager mgr(4096, *tls);
    mgr.disconnect_all();
    EXPECT_TRUE(mgr.empty());
}

TEST(mcp, manager_has_tool_empty) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPManager mgr(4096, *tls);
    EXPECT_FALSE(mgr.has_tool("nonexistent_tool"));
}

TEST(mcp, manager_custom_buffer) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPManager mgr(16384, *tls);
    EXPECT_TRUE(mgr.empty());
}

// ============================================================
// 配置结构体测试
// ============================================================
TEST(mcp, config_default_disabled) {
    cfg::MCPServerConfig c;
    EXPECT_FALSE(c.disabled);
}

TEST(mcp, config_set_fields) {
    cfg::MCPServerConfig c;
    c.disabled = false;
    c.url = "https://example.com:9090";
    c.command = "/usr/bin/my-mcp-server";
    c.args = {"--verbose", "--port", "3000"};
    EXPECT_FALSE(c.disabled);
    EXPECT_STREQ(c.url.data(), "https://example.com:9090");
    EXPECT_STREQ(c.command.data(), "/usr/bin/my-mcp-server");
    EXPECT_EQ(c.args.size(), (size_t)3);
}

// ============================================================
// 健全性：MCPClient 生命周期
// ============================================================
TEST(mcp, client_lifecycle) {
    auto tls = ben_gear::net::create_default_tls_engine(); mcp::MCPClient client(4096, *tls);
    EXPECT_FALSE(client.is_connected());
    client.disconnect();  // 析构前 disconnect 安全
    EXPECT_FALSE(client.is_connected());
}
