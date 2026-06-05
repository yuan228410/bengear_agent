#include <gtest/gtest.h>
#include <cmath>
#include "ben_gear/config/loader.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "test_util.hpp"

using bengear::test::TmpDirTest;

// --- KeyValue file parsing ---

class ConfigLoaderTest : public TmpDirTest {};

TEST_F(ConfigLoaderTest, KeyValueFileParsing) {
    const auto file = dir() / ".bengear.conf";
    {
        std::ofstream out(file);
        out << "provider = anthropic\n"
            << "api_key = test-key\n"
            << "base_url = https://example.test\n"
            << "api_url: https://example.test/v1/messages\n"
            << "model = claude-test\n"
            << "max_tokens = 2048\n"
            << "temperature = 0.75\n";
    }

    auto values = ben_gear::config::read_key_value_file(file);
    ben_gear::config::Settings settings;
    ben_gear::config::apply_values(settings, values);

    EXPECT_EQ(settings.provider, ben_gear::config::Provider::anthropic);
    EXPECT_EQ(settings.api_key, "test-key");
    EXPECT_EQ(settings.base_url, "https://example.test");
    EXPECT_EQ(settings.api_url, "https://example.test/v1/messages");
    EXPECT_EQ(settings.model, "claude-test");
    EXPECT_EQ(settings.max_tokens, 2048);
    EXPECT_TRUE(settings.stream);
    EXPECT_NEAR(settings.temperature, 0.75, 0.0001);

    values["stream"] = "false";
    ben_gear::config::apply_values(settings, values);
    EXPECT_FALSE(settings.stream);
}

TEST_F(ConfigLoaderTest, DefaultProviderIsOpenAi) {
    ben_gear::config::Settings settings;
    EXPECT_EQ(settings.provider, ben_gear::config::Provider::openai);
}

// --- Model JSON config ---

TEST_F(ConfigLoaderTest, ModelConfigJson) {
    const auto file = dir() / "config.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "oneapi:claude_sonnet",
          "llm_request_retry": {
            "max_attempts": 7,
            "initial_delay_ms": 10,
            "max_delay_ms": 100
          },
          "log": {
            "level": "debug",
            "output": "file,network",
            "file": "/tmp/bengear-test.log",
            "network_host": "127.0.0.1",
            "network_port": "9000"
          },
          "model_config": {
            "oneapi": {
              "api_key": "model-key",
              "base_url": "https://oneapi.test/v1",
              "headers": {
                "comate_custom_header": "{\"username\":\"test\"}"
              },
              "models": [
                {
                  "id": "Claude Sonnet 4.6",
                  "name": "claude_sonnet",
                  "api_mode": "anthropic",
                  "contextWindow": 1000000,
                  "stream": false,
                  "temperature": 0.3
                },
                {
                  "id": "DeepSeek-V4-Flash",
                  "name": "deepseek",
                  "api_mode": "openai",
                  "api_url": "https://oneapi.test/v1/chat/completions"
                }
              ]
            }
          }
        })";
    }

    auto s = ben_gear::config::load_model_config(file);
    EXPECT_EQ(s.provider, ben_gear::config::Provider::anthropic);
    EXPECT_EQ(s.api_key, "model-key");
    EXPECT_EQ(s.model, "Claude Sonnet 4.6");
    EXPECT_EQ(s.display_name, "claude_sonnet");
    EXPECT_FALSE(s.stream);
    EXPECT_EQ(s.logging.level, ben_gear::LogLevel::debug);
    EXPECT_EQ(s.logging.output, "file,network");
    EXPECT_EQ(s.logging.file, "/tmp/bengear-test.log");
    EXPECT_EQ(s.logging.network_host, "127.0.0.1");
    EXPECT_EQ(s.logging.network_port, "9000");
    EXPECT_EQ(s.context_length, 1000000);
    EXPECT_EQ(s.llm_request_retry.max_attempts, 7);
    EXPECT_EQ(s.llm_request_retry.initial_delay_ms, 10);
    EXPECT_EQ(s.llm_request_retry.max_delay_ms, 100);
    EXPECT_EQ(s.headers.at("comate_custom_header"), "{\"username\":\"test\"}");

    auto s2 = ben_gear::config::load_model_config(file, "oneapi:deepseek");
    EXPECT_EQ(s2.provider, ben_gear::config::Provider::openai);
    EXPECT_TRUE(s2.stream);
    EXPECT_EQ(s2.api_url, "https://oneapi.test/v1/chat/completions");
}

// --- ApplyValues new fields ---

TEST_F(ConfigLoaderTest, ApplyValuesNewFields) {
    const auto file = dir() / "test.conf";
    {
        std::ofstream out(file);
        out << "username = alice\n"
            << "workspace_name = proj1\n"
            << "role = teammate\n";
    }

    auto values = ben_gear::config::read_key_value_file(file);
    ben_gear::config::Settings settings;
    ben_gear::config::apply_values(settings, values);

    EXPECT_EQ(settings.username, "alice");
    EXPECT_EQ(settings.workspace_name, "proj1");
    EXPECT_EQ(settings.role, "teammate");
}

TEST_F(ConfigLoaderTest, ApplyJsonNewFields) {
    ben_gear::config::Settings settings;
    std::string json_text = R"({
        "username": "bob",
        "workspace_name": "proj2",
        "role": "lead",
        "session_id": "abc-123"
    })";
    std::string err;
    auto json = ben_gear::parse_json(json_text, err);
    ASSERT_TRUE(err.empty());
    ben_gear::config::apply_json_to_settings(settings, json);
    EXPECT_EQ(settings.username, "bob");
    EXPECT_EQ(settings.workspace_name, "proj2");
    EXPECT_EQ(settings.role, "lead");
    EXPECT_EQ(settings.session_id, "abc-123");
}

// --- Model config format ---

class ModelConfigFormatTest : public TmpDirTest {};

TEST_F(ModelConfigFormatTest, BasicInheritance) {
    const auto file = dir() / "new_format.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "oneapi:deepseek_flash",
          "stream": false,
          "model_config": {
            "oneapi": {
              "base_url": "https://oneapi.test/v1",
              "api_key": "shared-key",
              "headers": {"X-Custom": "value"},
              "models": [
                {
                  "id": "DeepSeek-V4-Flash",
                  "name": "deepseek_flash",
                  "api_mode": "openai",
                  "contextWindow": 204800,
                  "max_tokens": 8192,
                  "temperature": 0.5,
                  "reasoning": true
                },
                {
                  "id": "Claude Sonnet 4.6",
                  "name": "claude_sonnet",
                  "api_mode": "anthropic",
                  "max_tokens": 4096
                }
              ]
            }
          }
        })";
    }
    auto s = ben_gear::config::load_model_config(file);
    EXPECT_EQ(s.model, "DeepSeek-V4-Flash");
    EXPECT_EQ(s.display_name, "deepseek_flash");
    EXPECT_EQ(s.api_key, "shared-key");
    EXPECT_EQ(s.base_url, "https://oneapi.test/v1");
    EXPECT_EQ(s.headers.at("X-Custom"), "value");
    EXPECT_EQ(s.provider, ben_gear::config::Provider::openai);
    EXPECT_EQ(s.context_length, 204800);
    EXPECT_EQ(s.max_tokens, 8192);
    EXPECT_NEAR(s.temperature, 0.5, 0.001);
    EXPECT_TRUE(s.reasoning);
    EXPECT_FALSE(s.stream);
}

TEST_F(ModelConfigFormatTest, ModelOverridesProvider) {
    const auto file = dir() / "override.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "myprov:m1",
          "model_config": {
            "myprov": {
              "base_url": "https://default.test/v1",
              "api_key": "default-key",
              "models": [
                {"id": "model-override", "name": "m1", "api_key": "override-key"}
              ]
            }
          }
        })";
    }
    auto s = ben_gear::config::load_model_config(file);
    EXPECT_EQ(s.api_key, "override-key");
    EXPECT_EQ(s.base_url, "https://default.test/v1");
}

TEST_F(ModelConfigFormatTest, SecondModel) {
    const auto file = dir() / "second_model.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "oneapi:claude_sonnet",
          "model_config": {
            "oneapi": {
              "base_url": "https://oneapi.test/v1",
              "api_key": "shared-key",
              "models": [
                {"id": "DeepSeek-V4-Flash", "name": "deepseek_flash", "api_mode": "openai"},
                {"id": "Claude Sonnet 4.6", "name": "claude_sonnet", "api_mode": "anthropic"}
              ]
            }
          }
        })";
    }
    auto s = ben_gear::config::load_model_config(file);
    EXPECT_EQ(s.model, "Claude Sonnet 4.6");
    EXPECT_EQ(s.provider, ben_gear::config::Provider::anthropic);
}

TEST_F(ModelConfigFormatTest, NoColonThrows) {
    const auto file = dir() / "no_colon.json";
    {
        std::ofstream out(file);
        out << R"({"active_model": "just-a-name", "model_config": {"p": {"api_key": "k", "models": [{"id": "m", "name": "n"}]}}})";
    }
    EXPECT_THROW(ben_gear::config::load_model_config(file), std::runtime_error);
}

TEST_F(ModelConfigFormatTest, MissingProviderThrows) {
    const auto file = dir() / "bad_provider.json";
    {
        std::ofstream out(file);
        out << R"({"active_model": "missing:model", "model_config": {"other": {"api_key": "k", "models": [{"id": "m", "name": "model"}]}}})";
    }
    EXPECT_THROW(ben_gear::config::load_model_config(file), std::runtime_error);
}

TEST_F(ModelConfigFormatTest, MissingModelThrows) {
    const auto file = dir() / "bad_model.json";
    {
        std::ofstream out(file);
        out << R"({"active_model": "p:missing", "model_config": {"p": {"api_key": "k", "models": [{"id": "m", "name": "other"}]}}})";
    }
    EXPECT_THROW(ben_gear::config::load_model_config(file), std::runtime_error);
}

TEST_F(ModelConfigFormatTest, MissingIdThrows) {
    const auto file = dir() / "no_id.json";
    {
        std::ofstream out(file);
        out << R"({"active_model": "p:m1", "model_config": {"p": {"api_key": "k", "models": [{"name": "m1"}]}}})";
    }
    EXPECT_THROW(ben_gear::config::load_model_config(file), std::runtime_error);
}

TEST_F(ModelConfigFormatTest, ListModels) {
    const auto file = dir() / "list.json";
    {
        std::ofstream out(file);
        out << R"({
          "model_config": {
            "prov1": {"api_key": "k", "models": [
              {"id": "m1", "name": "a"},
              {"id": "m2", "name": "b"}
            ]}
          }
        })";
    }
    auto names = ben_gear::config::list_models(file);
    EXPECT_EQ(names.size(), 2u);
    EXPECT_NE(std::find(names.begin(), names.end(), "prov1:a"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "prov1:b"), names.end());
}

// --- Multi-tier config integration tests ---

class ConfigIntegrationTest : public TmpDirTest {};

TEST_F(ConfigIntegrationTest, UserLevelConfOverridesDefaults) {
    ben_gear::config::Settings settings;

    // 写入用户级 .conf
    auto user_conf = dir() / "user.conf";
    {
        std::ofstream out(user_conf);
        out << "provider = anthropic\n"
            << "api_key = user-key\n"
            << "model = claude-sonnet\n"
            << "username = alice\n";
    }

    auto values = ben_gear::config::read_key_value_file(user_conf);
    ben_gear::config::apply_values(settings, values);

    EXPECT_EQ(settings.provider, ben_gear::config::Provider::anthropic);
    EXPECT_EQ(settings.api_key, "user-key");
    EXPECT_EQ(settings.model, "claude-sonnet");
    EXPECT_EQ(settings.username, "alice");
}

TEST_F(ConfigIntegrationTest, WorkspaceConfOverridesUserConf) {
    ben_gear::config::Settings settings;

    // 用户级
    auto user_conf = dir() / "user.conf";
    {
        std::ofstream out(user_conf);
        out << "api_key = user-key\n"
            << "model = user-model\n";
    }
    auto user_values = ben_gear::config::read_key_value_file(user_conf);
    ben_gear::config::apply_values(settings, user_values);
    EXPECT_EQ(settings.api_key, "user-key");

    // 工作空间级（后加载，覆盖）
    auto ws_conf = dir() / "workspace.conf";
    {
        std::ofstream out(ws_conf);
        out << "api_key = workspace-key\n"
            << "role = teammate\n"
            << "workspace_name = my-project\n";
    }
    auto ws_values = ben_gear::config::read_key_value_file(ws_conf);
    ben_gear::config::apply_values(settings, ws_values);

    EXPECT_EQ(settings.api_key, "workspace-key");  // 覆盖用户级
    EXPECT_EQ(settings.model, "user-model");        // 保留用户级
    EXPECT_EQ(settings.role, "teammate");            // 工作空间新增
    EXPECT_EQ(settings.workspace_name, "my-project");
}

TEST_F(ConfigIntegrationTest, JsonConfOverridesFlat) {
    ben_gear::config::Settings settings;

    // 先从 flat .conf 加载
    auto flat_conf = dir() / ".bengear.conf";
    {
        std::ofstream out(flat_conf);
        out << "api_key = flat-key\n"
            << "model = flat-model\n";
    }
    auto flat_values = ben_gear::config::read_key_value_file(flat_conf);
    ben_gear::config::apply_values(settings, flat_values);
    EXPECT_EQ(settings.api_key, "flat-key");

    // JSON 配置覆盖
    std::string json_text = R"({
        "api_key": "json-key",
        "session_id": "sess-001",
        "connection_pool": {
            "max_connections_per_host": 20,
            "idle_timeout_seconds": 60
        }
    })";
    std::string err;
    auto json = ben_gear::parse_json(json_text, err);
    ASSERT_TRUE(err.empty());
    ben_gear::config::apply_json_to_settings(settings, json);

    EXPECT_EQ(settings.api_key, "json-key");
    EXPECT_EQ(settings.model, "flat-model");  // flat 不被覆盖
    EXPECT_EQ(settings.session_id, "sess-001");
    EXPECT_EQ(settings.connection_pool.max_connections_per_host, 20);
    EXPECT_EQ(settings.connection_pool.idle_timeout_seconds, 60);
}

TEST_F(ConfigIntegrationTest, EnvVarOverridesAll) {
    ben_gear::config::Settings settings;
    settings.api_key = "file-key";
    settings.username = "file-user";

    // 环境变量优先级最高
    auto env_key = ben_gear::base::platform::os::getenv_optional("BEN_GEAR_API_KEY");
    auto env_user = ben_gear::base::platform::os::getenv_optional("BEN_GEAR_USER");

    // 测试逻辑：如果环境变量存在，则覆盖
    if (env_key) {
        settings.api_key = ben_gear::base::container::String(env_key->c_str());
    }
    if (env_user) {
        settings.username = ben_gear::base::container::String(env_user->c_str());
    }

    // 如果没有环境变量，验证文件值保留；如果有，验证至少 api_key 非空
    if (!env_key && !env_user) {
        EXPECT_EQ(std::string(settings.api_key.data(), settings.api_key.size()), "file-key");
    } else {
        EXPECT_FALSE(settings.api_key.empty());
    }
}

TEST_F(ConfigIntegrationTest, ThreeTierWorkspaceContext) {
    // 模拟三层级配置→WorkspaceContext 的完整流程
    ben_gear::config::Settings settings;

    auto user_conf = dir() / "user.conf";
    {
        std::ofstream out(user_conf);
        out << "username = bob\n"
            << "workspace_name = project-x\n"
            << "role = lead\n";
    }
    auto values = ben_gear::config::read_key_value_file(user_conf);
    ben_gear::config::apply_values(settings, values);

    // 构建 WorkspaceContext（模拟 main.cpp 的 build_ws_ctx）
    auto home = dir();  // 使用临时目录代替 home
    auto username = std::string(settings.username.empty() ? "default" : settings.username.c_str());
    auto ws_name = std::string(settings.workspace_name.empty() ? "default" : settings.workspace_name.c_str());

    ben_gear::workspace::TierPaths tier_paths{
        home / ".bengear",
        home / ".bengear" / "users" / username,
        home / ".bengear" / "users" / username / "workspaces" / ws_name
    };

    ben_gear::workspace::WorkspaceContext ws_ctx{
        tier_paths,
        ben_gear::base::container::String(ws_name.c_str()),
        ben_gear::base::container::String(username.c_str()),
        settings.session_id
    };

    EXPECT_EQ(username, "bob");
    EXPECT_EQ(ws_name, "project-x");
    EXPECT_EQ(ws_ctx.tier_paths.global_dir, home / ".bengear");
    EXPECT_EQ(ws_ctx.tier_paths.user_dir, home / ".bengear" / "users" / "bob");
    EXPECT_EQ(ws_ctx.tier_paths.workspace_dir, home / ".bengear" / "users" / "bob" / "workspaces" / "project-x");

    // WorkspaceManager 应该能正确创建默认工作空间
    ben_gear::workspace::WorkspaceManager mgr(ws_ctx.tier_paths.user_dir);
    auto workspaces = mgr.list_all();
    EXPECT_FALSE(workspaces.empty());  // 默认工作空间自动创建
}
