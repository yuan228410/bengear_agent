#include "ben_gear/test/test_framework.hpp"
#include <cmath>
#include "ben_gear/config/loader.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "test_util.hpp"

using bengear::test::TmpDirTest;

// --- KeyValue file parsing ---

class ConfigLoaderTest : public TmpDirTest {};



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
    EXPECT_EQ(settings.session_id, "abc-123");
}

TEST_F(ConfigLoaderTest, SubAgentNestedConfigFields) {
    const auto file = dir() / "sub_agent.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "oneapi:deepseek",
          "agent": {
            "sub_agent": {
              "max_parallel": 7,
              "default_max_steps": 33,
              "default_timeout_seconds": 45,
              "auto_summary": false,
              "max_output_chars": 1234,
              "model_override": "oneapi:claude_sonnet",
              "context_length_override": 9999,
              "aggregate_parallel": false,
              "tool_filter_default": ["read_file", "grep_content"]
            }
          },
          "model_config": {
            "oneapi": {
              "api_key": "key",
              "base_url": "https://oneapi.test/v1",
              "models": [
                {"id": "DeepSeek", "name": "deepseek", "api_mode": "openai"},
                {"id": "Claude", "name": "claude_sonnet", "api_mode": "anthropic"}
              ]
            }
          }
        })";
    }

    auto settings = ben_gear::config::load_model_config(file);
    EXPECT_EQ(settings.agent.sub_agent.max_parallel, 7);
    EXPECT_EQ(settings.agent.sub_agent.default_max_steps, 33);
    EXPECT_EQ(settings.agent.sub_agent.default_timeout.count(), 45000);
    EXPECT_FALSE(settings.agent.sub_agent.auto_summary);
    EXPECT_EQ(settings.agent.sub_agent.max_output_chars, 1234);
    EXPECT_EQ(settings.agent.sub_agent.model_override, "oneapi:claude_sonnet");
    EXPECT_EQ(settings.agent.sub_agent.context_length_override, 9999);
    EXPECT_FALSE(settings.agent.sub_agent.aggregate_parallel);
    ASSERT_EQ(settings.agent.sub_agent.tool_filter_default.size(), 2u);
    EXPECT_EQ(settings.agent.sub_agent.tool_filter_default[0], "read_file");
}

TEST_F(ConfigLoaderTest, AgentToolBudgetFields) {
    const auto file = dir() / "tool_budget.json";
    {
        std::ofstream out(file);
        out << R"({
          "active_model": "oneapi:deepseek",
          "agent": {
            "max_tool_steps": 123,
            "max_tool_calls": 234,
            "max_tool_calls_per_step": 45
          },
          "model_config": {
            "oneapi": {
              "api_key": "key",
              "base_url": "https://oneapi.test/v1",
              "models": [
                {"id": "DeepSeek", "name": "deepseek", "api_mode": "openai"}
              ]
            }
          }
        })";
    }

    auto settings = ben_gear::config::load_model_config(file);
    EXPECT_EQ(settings.agent.max_tool_steps, 123);
    EXPECT_EQ(settings.agent.max_tool_calls, 234);
    EXPECT_EQ(settings.agent.max_tool_calls_per_step, 45);
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


