#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/llm/provider_error.hpp"
#include "ben_gear/llm/cooldown_tracker.hpp"
#include "ben_gear/config/loader.hpp"

#include <filesystem>
#include <fstream>

using namespace ben_gear::llm;
using namespace ben_gear::config;
namespace container = ben_gear::base::container;

// ==================== ProviderError ====================

TEST(ProviderErrorTest, Classify429) {
    EXPECT_EQ(classify_http_error(429), ProviderErrorKind::rate_limit);
}

TEST(ProviderErrorTest, Classify401) {
    EXPECT_EQ(classify_http_error(401), ProviderErrorKind::auth_error);
}

TEST(ProviderErrorTest, Classify503) {
    EXPECT_EQ(classify_http_error(503), ProviderErrorKind::transient);
}

TEST(ProviderErrorTest, Classify400ContextOverflow) {
    EXPECT_EQ(classify_http_error(400, "context_length exceeded"), ProviderErrorKind::context_overflow);
}

TEST(ProviderErrorTest, Classify400BadRequest) {
    EXPECT_EQ(classify_http_error(400, "invalid parameter"), ProviderErrorKind::bad_request);
}

TEST(ProviderErrorTest, Classify404Model) {
    EXPECT_EQ(classify_http_error(404, "model not found"), ProviderErrorKind::model_not_found);
}

TEST(ProviderErrorTest, Retryable) {
    EXPECT_TRUE(is_retryable_error(ProviderErrorKind::rate_limit));
    EXPECT_TRUE(is_retryable_error(ProviderErrorKind::transient));
    EXPECT_TRUE(is_retryable_error(ProviderErrorKind::timeout));
    EXPECT_TRUE(is_retryable_error(ProviderErrorKind::model_not_found));
    EXPECT_FALSE(is_retryable_error(ProviderErrorKind::auth_error));
    EXPECT_FALSE(is_retryable_error(ProviderErrorKind::bad_request));
    EXPECT_FALSE(is_retryable_error(ProviderErrorKind::context_overflow));
}

// ==================== CooldownTracker ====================

TEST(CooldownTrackerTest, InitiallyNotInCooldown) {
    CooldownTracker ct;
    EXPECT_FALSE(ct.is_in_cooldown("gpt-4o"));
    EXPECT_EQ(ct.failure_count("gpt-4o"), 0);
    EXPECT_EQ(ct.cooldown_remaining("gpt-4o").count(), 0);
}

TEST(CooldownTrackerTest, FailurePutsInCooldown) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit);
    EXPECT_TRUE(ct.is_in_cooldown("gpt-4o"));
    EXPECT_EQ(ct.failure_count("gpt-4o"), 1);
    EXPECT_GT(ct.cooldown_remaining("gpt-4o").count(), 0);
}

TEST(CooldownTrackerTest, SuccessClearsCooldown) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit);
    EXPECT_TRUE(ct.is_in_cooldown("gpt-4o"));
    ct.record_success("gpt-4o");
    EXPECT_FALSE(ct.is_in_cooldown("gpt-4o"));
    EXPECT_EQ(ct.failure_count("gpt-4o"), 0);
}

TEST(CooldownTrackerTest, DifferentModelsIndependent) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit);
    EXPECT_TRUE(ct.is_in_cooldown("gpt-4o"));
    EXPECT_FALSE(ct.is_in_cooldown("claude-3"));
}

TEST(CooldownTrackerTest, RetryAfterOverridesBackoff) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit, 60);
    EXPECT_GE(ct.cooldown_remaining("gpt-4o").count(), 55);
}

TEST(CooldownTrackerTest, ProbeAllowedAfterInterval) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit);
    EXPECT_TRUE(ct.try_probe("gpt-4o"));
    EXPECT_FALSE(ct.try_probe("gpt-4o"));
}

TEST(CooldownTrackerTest, Reset) {
    CooldownTracker ct;
    ct.record_failure("gpt-4o", ProviderErrorKind::rate_limit);
    ct.record_failure("claude-3", ProviderErrorKind::transient);
    ct.reset();
    EXPECT_FALSE(ct.is_in_cooldown("gpt-4o"));
    EXPECT_FALSE(ct.is_in_cooldown("claude-3"));
}

// ==================== Fallback 解析集成测试 ====================

// 创建临时配置文件用于测试
class FallbackConfigTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir_;
    std::filesystem::path config_path_;

    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "bengear_test_fallback";
        std::filesystem::create_directories(tmp_dir_);
        config_path_ = tmp_dir_ / "test_config.json";
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }

    void write_config(const std::string& content) {
        std::ofstream f(config_path_, std::ios::binary);
        f << content;
    }
};

TEST_F(FallbackConfigTest, FallbackModelsUseProviderModelFormat) {
    // 写入包含 fallback_models 的配置
    write_config(R"({
        "active_model": "oneapi:deepseek_flash",
        "stream": true,
        "model_config": {
            "oneapi": {
                "base_url": "https://oneapi.example.com/v1",
                "api_key": "test-key-oneapi",
                "models": [
                    {
                        "id": "DeepSeek-V4-Flash",
                        "name": "deepseek_flash",
                        "api_mode": "openai",
                        "max_tokens": 8192,
                        "temperature": 0.3
                    },
                    {
                        "id": "Claude-Sonnet",
                        "name": "claude_sonnet",
                        "api_mode": "anthropic",
                        "anthropic_api_version": "2026-01-01",
                        "max_tokens": 4096,
                        "temperature": 0.5
                    }
                ]
            },
            "direct": {
                "base_url": "https://api.openai.com/v1",
                "api_key": "test-key-direct",
                "models": [
                    {
                        "id": "gpt-4o",
                        "name": "gpt4o",
                        "api_mode": "openai",
                        "max_tokens": 4096,
                        "temperature": 0.7
                    }
                ]
            }
        },
        "fallback_models": ["oneapi:claude_sonnet", "direct:gpt4o"]
    })");

    auto settings = load_model_config(config_path_, "oneapi:deepseek_flash");

    // 验证主模型配置
    EXPECT_EQ(settings.model.to_std_string(), "DeepSeek-V4-Flash");
    EXPECT_EQ(settings.provider, Provider::openai);
    EXPECT_EQ(settings.base_url.to_std_string(), "https://oneapi.example.com/v1");
    EXPECT_EQ(settings.api_key.to_std_string(), "test-key-oneapi");

    // 验证 fallback_models 列表
    ASSERT_EQ(settings.fallback_models.size(), 2u);
    EXPECT_EQ(settings.fallback_models[0], "oneapi:claude_sonnet");
    EXPECT_EQ(settings.fallback_models[1], "direct:gpt4o");

    // 验证 resolved_fallbacks 包含完整配置
    ASSERT_EQ(settings.resolved_fallbacks.size(), 2u);

    // 第一个 fallback: oneapi:claude_sonnet
    {
        auto it = settings.resolved_fallbacks.find("oneapi:claude_sonnet");
        ASSERT_NE(it, settings.resolved_fallbacks.end());
        const auto& fb = it->second;
        EXPECT_EQ(fb.model.to_std_string(), "Claude-Sonnet");
        EXPECT_EQ(fb.provider, Provider::anthropic);
        EXPECT_EQ(fb.base_url.to_std_string(), "https://oneapi.example.com/v1");
        EXPECT_EQ(fb.api_key.to_std_string(), "test-key-oneapi");
        EXPECT_EQ(fb.max_tokens, 4096);
        EXPECT_DOUBLE_EQ(fb.temperature, 0.5);
        EXPECT_EQ(fb.anthropic_api_version.to_std_string(), "2026-01-01");
    }

    // 第二个 fallback: direct:gpt4o
    {
        auto it = settings.resolved_fallbacks.find("direct:gpt4o");
        ASSERT_NE(it, settings.resolved_fallbacks.end());
        const auto& fb = it->second;
        EXPECT_EQ(fb.model.to_std_string(), "gpt-4o");
        EXPECT_EQ(fb.provider, Provider::openai);
        EXPECT_EQ(fb.base_url.to_std_string(), "https://api.openai.com/v1");
        EXPECT_EQ(fb.api_key.to_std_string(), "test-key-direct");
        EXPECT_EQ(fb.max_tokens, 4096);
        EXPECT_DOUBLE_EQ(fb.temperature, 0.7);
    }
}

TEST_F(FallbackConfigTest, FallbackInheritsGlobalConfig) {
    write_config(R"({
        "active_model": "oneapi:deepseek_flash",
        "stream": false,
        "model_config": {
            "oneapi": {
                "base_url": "https://oneapi.example.com/v1",
                "api_key": "test-key",
                "models": [
                    {
                        "id": "DeepSeek-V4-Flash",
                        "name": "deepseek_flash",
                        "api_mode": "openai",
                        "max_tokens": 8192
                    },
                    {
                        "id": "Claude-Sonnet",
                        "name": "claude_sonnet",
                        "api_mode": "anthropic",
                        "max_tokens": 4096
                    }
                ]
            }
        },
        "fallback_models": ["oneapi:claude_sonnet"]
    })");

    auto settings = load_model_config(config_path_, "oneapi:deepseek_flash");

    // 验证 fallback 继承了全局 stream 设置
    auto it = settings.resolved_fallbacks.find("oneapi:claude_sonnet");
    ASSERT_NE(it, settings.resolved_fallbacks.end());
    EXPECT_EQ(it->second.stream, false);
}

TEST_F(FallbackConfigTest, ApplyLlmFieldsToSwitchesProvider) {
    // 验证 apply_llm_fields_to 正确切换 LLM 字段
    Settings primary;
    primary.provider = Provider::openai;
    primary.model = container::String("gpt-4o");
    primary.base_url = container::String("https://api.openai.com/v1");
    primary.api_key = container::String("key-openai");
    primary.max_tokens = 4096;
    primary.temperature = 0.7;
    primary.stream = true;

    Settings fallback;
    fallback.provider = Provider::anthropic;
    fallback.model = container::String("Claude-Sonnet");
    fallback.base_url = container::String("https://api.anthropic.com/v1");
    fallback.api_key = container::String("key-anthropic");
    fallback.max_tokens = 8192;
    fallback.temperature = 0.3;
    fallback.anthropic_api_version = container::String("2026-01-01");

    Settings target = primary;
    fallback.apply_llm_fields_to(target);

    // LLM 字段应被覆盖
    EXPECT_EQ(target.provider, Provider::anthropic);
    EXPECT_EQ(target.model.to_std_string(), "Claude-Sonnet");
    EXPECT_EQ(target.base_url.to_std_string(), "https://api.anthropic.com/v1");
    EXPECT_EQ(target.api_key.to_std_string(), "key-anthropic");
    EXPECT_EQ(target.max_tokens, 8192);
    EXPECT_DOUBLE_EQ(target.temperature, 0.3);
    EXPECT_EQ(target.anthropic_api_version.to_std_string(), "2026-01-01");

    // 非 LLM 字段应保持不变
    EXPECT_EQ(target.stream, true);
}

TEST_F(FallbackConfigTest, InvalidFallbackModelSkipped) {
    write_config(R"({
        "active_model": "oneapi:deepseek_flash",
        "model_config": {
            "oneapi": {
                "base_url": "https://oneapi.example.com/v1",
                "api_key": "test-key",
                "models": [
                    {
                        "id": "DeepSeek-V4-Flash",
                        "name": "deepseek_flash",
                        "api_mode": "openai",
                        "max_tokens": 8192
                    }
                ]
            }
        },
        "fallback_models": ["nonexistent:model_x"]
    })");

    // 不应抛异常，无效 fallback 被跳过
    auto settings = load_model_config(config_path_, "oneapi:deepseek_flash");
    ASSERT_EQ(settings.fallback_models.size(), 1u);
    // resolved_fallbacks 中不应有无效条目
    EXPECT_EQ(settings.resolved_fallbacks.size(), 0u);
}
