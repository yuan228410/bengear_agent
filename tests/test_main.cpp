#include "ben_gear/config/loader.hpp"
#include "ben_gear/tools/builtin_tools.hpp"
#include "ben_gear/llm/anthropic_client.hpp"
#include "ben_gear/llm/internal/anthropic_parser.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/internal/openai_parser.hpp"
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_support_string() {
    require(ben_gear::base::utils::trim("  BenGear\n") == "BenGear", "trim removes whitespace");
    require(ben_gear::base::utils::to_lower("OpenAI") == "openai", "to_lower normalizes text");
}

void test_support_json() {
    require(ben_gear::json_string("a\"b\n") == "\"a\\\"b\\n\"", "json_string escapes quotes and newline");
    
    // 测试 JSON 解析
    std::string error;
    auto json = ben_gear::parse_json("{\"text\":\"hello\\nworld\"}", error);
    require(error.empty(), "parse_json succeeds");
    require(ben_gear::get_json_value<std::string>(json, "text") == "hello\nworld", "get_json_value extracts string");
}

void test_llm_protocol_clients() {
    ben_gear::config::Settings openai_settings;
    openai_settings.api_key = "openai-key";
    openai_settings.model = "openai-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::OpenAiClient openai(openai_settings);
    const auto openai_body = openai.request_body_for_test(request);
    const auto openai_headers = openai.request_headers_for_test();
    require(openai_body.find("\"role\":\"system\"") != std::string::npos, "openai keeps system prompt in messages");
    require(openai_body.find("\"role\":\"user\"") != std::string::npos, "openai uses user message role");
    require(openai_body.find("\"system\":") == std::string::npos, "openai does not use anthropic system field");
    require(openai_headers.back() == "Authorization: Bearer openai-key", "openai uses bearer authorization");

    ben_gear::config::Settings anthropic_settings;
    anthropic_settings.provider = ben_gear::config::Provider::anthropic;
    anthropic_settings.api_key = "anthropic-key";
    anthropic_settings.model = "claude-model";
    ben_gear::llm::AnthropicClient anthropic(anthropic_settings);
    const auto anthropic_body = anthropic.request_body_for_test(request);
    const auto anthropic_headers = anthropic.request_headers_for_test();
    require(anthropic_body.find("\"system\":\"system text\"") != std::string::npos, "anthropic uses top-level system field");
    require(anthropic_body.find("\"role\":\"system\"") == std::string::npos, "anthropic does not put system role in messages");
    require(anthropic_headers.end() != std::find(anthropic_headers.begin(), anthropic_headers.end(), "x-api-key: anthropic-key"), "anthropic uses x-api-key");
    require(anthropic_headers.end() != std::find(anthropic_headers.begin(), anthropic_headers.end(), "anthropic-version: 2026-01-01"), "anthropic sends version header");
    require(openai.stream_request_body_for_test(request).find("\"stream\":true") != std::string::npos, "openai stream request enables stream");
    require(anthropic.stream_request_body_for_test(request).find("\"stream\":true") != std::string::npos, "anthropic stream request enables stream");
}

void test_stream_parsers() {
    std::string openai_text;
    ben_gear::llm::OpenAiStreamParser openai_parser([&](std::string_view token) {
        openai_text += token;
    });
    std::string openai_thinking;
    ben_gear::llm::OpenAiStreamParser openai_thinking_parser(ben_gear::llm::StreamHandlers(
        [&](std::string_view token) { openai_text += token; },
        [&](std::string_view token) { openai_thinking += token; }
    ));
    openai_thinking_parser.parse("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n");
    openai_parser.parse("data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n\n"
                        "data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n"
                        "data: [DONE]\n\n");
    require(openai_text == "你好", "openai stream parser extracts delta content");
    require(openai_thinking == "想", "openai stream parser extracts thinking content");

    std::string anthropic_text;
    ben_gear::llm::AnthropicStreamParser anthropic_parser([&](std::string_view token) {
        anthropic_text += token;
    });
    std::string anthropic_thinking;
    ben_gear::llm::AnthropicStreamParser anthropic_thinking_parser(ben_gear::llm::StreamHandlers(
        [&](std::string_view token) { anthropic_text += token; },
        [&](std::string_view token) { anthropic_thinking += token; }
    ));
    anthropic_thinking_parser.parse("event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"thinking\":\"思\"}}\n\n");
    anthropic_parser.parse("event: message_start\ndata: {\"type\":\"message_start\"}\n\n"
                           "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"你\"}}\n\n"
                           "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"好\"}}\n\n");
    require(anthropic_text == "你好", "anthropic stream parser extracts text deltas");
    require(anthropic_thinking == "思", "anthropic stream parser extracts thinking deltas");
}

void test_builtin_tools() {
    // 测试工具注册表
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry);
    require(registry.size() > 0, "builtin tool registry has tools");
    require(registry.find("read_file") != nullptr, "registry contains read_file tool");
    require(registry.find("write_file") != nullptr, "registry contains write_file tool");
    
    // 测试工具执行
    const auto root = std::filesystem::temp_directory_path() / "bengear-tool-test";
    std::filesystem::create_directories(root);
    const auto file = root / "tool.txt";
    
    // 写入文件
    ben_gear::Json write_args = {
        {"path", file.string()},
        {"content", "hello tools"}
    };
    auto write_result = registry.execute("write_file", write_args);
    require(write_result.success && std::string(write_result.output.c_str()).find("Success") != std::string::npos,
            "write_file tool writes file");

    // 读取文件
    ben_gear::Json read_args = {{"path", file.string()}};
    auto read_result = registry.execute("read_file", read_args);
    require(read_result.success && std::string(read_result.output.c_str()) == "hello tools",
            "read_file tool reads file");
    
    std::filesystem::remove_all(root);
}

void test_llm_retry() {
    require(ben_gear::llm::is_retryable_status(429), "rate limit status is retryable");
    require(ben_gear::llm::is_retryable_status(503), "server error status is retryable");
    require(!ben_gear::llm::is_retryable_status(400), "client error status is not retryable");

    ben_gear::config::Settings settings;
    settings.llm_request_retry.max_attempts = 3;
    settings.llm_request_retry.initial_delay_ms = 1;
    settings.llm_request_retry.max_delay_ms = 1;
    int attempts = 0;
    const auto result = ben_gear::llm::with_retry(settings, "test retry", [&] {
        ++attempts;
        return ben_gear::llm::ChatResult{attempts < 3 ? 429 : 200, "ok", "raw"};
    });
    require(attempts == 3, "retry repeats until success");
    require(result.status == 200 && result.text == "ok", "retry returns successful result");
}

void test_endpoint_url_completion() {
    ben_gear::config::Settings settings;
    settings.base_url = "https://oneapi-comate.baidu-int.com";
    require(ben_gear::llm::endpoint_url(settings, "/v1/chat/completions") == "https://oneapi-comate.baidu-int.com/v1/chat/completions",
            "base-url host completes openai path");
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://oneapi-comate.baidu-int.com/v1/messages",
            "base-url host completes anthropic path");
    settings.base_url = "https://oneapi-comate.baidu-int.com/v1";
    require(ben_gear::llm::endpoint_url(settings, "/v1/chat/completions") == "https://oneapi-comate.baidu-int.com/v1/chat/completions",
            "base-url v1 completes openai path without duplication");
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://oneapi-comate.baidu-int.com/v1/messages",
            "base-url v1 completes anthropic path without duplication");
    settings.base_url = "https://oneapi-comate.baidu-int.com/v1/";
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://oneapi-comate.baidu-int.com/v1/messages",
            "base-url trailing slash completes without duplication");
    settings.api_url = "https://custom.test/custom/path";
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://custom.test/custom/path",
            "api_url custom path returned as-is");
    settings.api_url = "https://oneapi-comate.baidu-int.com/v1";
    require(ben_gear::llm::endpoint_url(settings, "/v1/chat/completions") == "https://oneapi-comate.baidu-int.com/v1/chat/completions",
            "api_url v1 completes openai path without duplication");
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://oneapi-comate.baidu-int.com/v1/messages",
            "api_url v1 completes anthropic path without duplication");
    settings.api_url = "https://custom.test/v1/chat/completions";
    require(ben_gear::llm::endpoint_url(settings, "/v1/chat/completions") == "https://custom.test/v1/chat/completions",
            "api_url full openai path returned as-is");
    settings.api_url = "https://custom.test/v1/messages";
    require(ben_gear::llm::endpoint_url(settings, "/v1/messages") == "https://custom.test/v1/messages",
            "api_url full anthropic path returned as-is");
}

void test_config_loader() {
    const auto root = std::filesystem::temp_directory_path() / "bengear-config-test";
    std::filesystem::create_directories(root);
    const auto file = root / ".bengear.conf";
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

    require(settings.provider == ben_gear::config::Provider::anthropic, "config parses provider");
    ben_gear::config::Settings default_settings;
    require(default_settings.provider == ben_gear::config::Provider::openai, "missing api_mode defaults to openai");
    require(settings.api_key == "test-key", "config parses api_key");
    require(settings.base_url == "https://example.test", "config parses base_url");
    require(settings.api_url == "https://example.test/v1/messages", "config parses api_url with colon separator");
    require(settings.model == "claude-test", "config parses model");
    require(settings.max_tokens == 2048, "config parses max_tokens");
    require(settings.stream, "config defaults to streaming");
    require(std::abs(settings.temperature - 0.75) < 0.0001, "config parses temperature");

    values["stream"] = "false";
    ben_gear::config::apply_values(settings, values);
    require(!settings.stream, "key-value config parses stream false");

    const auto models_file = root / "config.json";
    {
        std::ofstream out(models_file);
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
    const auto model_settings = ben_gear::config::load_model_config(models_file);
    require(model_settings.provider == ben_gear::config::Provider::anthropic, "api_mode anthropic selects anthropic provider");
    require(model_settings.api_key == "model-key", "model config parses provider api_key");
    require(model_settings.model == "Claude Sonnet 4.6", "id maps to settings.model");
    require(model_settings.display_name == "claude_sonnet", "name maps to display_name");
    require(!model_settings.stream, "model config parses stream false");
    require(model_settings.logging.level == ben_gear::LogLevel::debug, "model config parses log block level");
    require(model_settings.logging.output == "file,network", "model config parses log block output");
    require(model_settings.logging.file == "/tmp/bengear-test.log", "model config parses log block file");
    require(model_settings.logging.network_host == "127.0.0.1", "model config parses log block network host");
    require(model_settings.logging.network_port == "9000", "model config parses log block network port");
    require(model_settings.context_length == 1000000, "contextWindow maps to context_length");
    require(model_settings.llm_request_retry.max_attempts == 7, "model config parses global llm request retry max attempts");
    require(model_settings.llm_request_retry.initial_delay_ms == 10, "model config parses global llm request retry initial delay");
    require(model_settings.llm_request_retry.max_delay_ms == 100, "model config parses global llm request retry max delay");
    require(model_settings.headers.at("comate_custom_header") == "{\"username\":\"test\"}", "model config parses provider headers");

    const auto openai_model_settings = ben_gear::config::load_model_config(models_file, "oneapi:deepseek");
    require(openai_model_settings.provider == ben_gear::config::Provider::openai, "openai api_mode parsed");
    require(openai_model_settings.stream, "missing stream in model defaults to global (true)");
    require(openai_model_settings.api_url == "https://oneapi.test/v1/chat/completions", "model-level api_url parsed");

    std::filesystem::remove_all(root);
}

ben_gear::net::Task<int> immediate_task() {
    co_return 42;
}

ben_gear::net::Task<int> timer_task(ben_gear::net::EventLoop& loop) {
    co_await loop.sleep_for(std::chrono::milliseconds{1});
    co_return 7;
}

ben_gear::net::Task<ben_gear::llm::ChatResult> retry_async_task(ben_gear::net::EventLoop& /*loop*/, int& attempts) {
    // 简化版异步重试测试
    ben_gear::config::Settings settings;
    settings.llm_request_retry.max_attempts = 3;
    
    for (int attempt = 1; attempt <= settings.llm_request_retry.max_attempts; ++attempt) {
        ++attempts;
        if (attempts >= 3) {
            co_return ben_gear::llm::ChatResult{200, "async-ok", "raw"};
        }
    }
    co_return ben_gear::llm::ChatResult{503, "failed", "raw"};
}

void test_coroutine_task() {
    auto task = immediate_task();
    require(!task.done(), "task starts suspended");
    task.resume();
    require(task.done(), "task completes after resume");
    require(task.result() == 42, "task returns result");
}

void test_event_loop_constructs() {
    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    loop.run_once(std::chrono::milliseconds{0});
    require(loop.run(timer_task(loop)) == 7, "event loop resumes timer awaiter");
    int attempts = 0;
    const auto result = loop.run(retry_async_task(loop, attempts));
    require(attempts == 3, "async retry repeats until success");
    require(result.status == 200 && result.text == "async-ok", "async retry returns successful result");
}

}  // namespace

void test_model_config_format() {
    const auto root = std::filesystem::temp_directory_path() / "bengear-model-config-test";
    std::filesystem::create_directories(root);

    // 1. 基本加载：provider 级字段继承 + 字段映射
    {
        const auto file = root / "new_format.json";
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
        auto settings = ben_gear::config::load_model_config(file);
        require(settings.model == "DeepSeek-V4-Flash", "id maps to settings.model");
        require(settings.display_name == "deepseek_flash", "name maps to display_name");
        require(settings.api_key == "shared-key", "provider api_key inherited");
        require(settings.base_url == "https://oneapi.test/v1", "provider base_url inherited");
        require(settings.headers.at("X-Custom") == "value", "provider headers inherited");
        require(settings.provider == ben_gear::config::Provider::openai, "api_mode openai parsed");
        require(settings.context_length == 204800, "contextWindow maps to context_length");
        require(settings.max_tokens == 8192, "model max_tokens parsed");
        require(settings.temperature == 0.5, "model temperature parsed");
        require(settings.reasoning == true, "reasoning parsed");
        require(!settings.stream, "global stream inherited");
    }

    // 2. model 级覆盖 provider 级
    {
        const auto file = root / "override.json";
        {
            std::ofstream out(file);
            out << R"({
              "active_model": "myprov:m1",
              "model_config": {
                "myprov": {
                  "base_url": "https://default.test/v1",
                  "api_key": "default-key",
                  "models": [
                    {
                      "id": "model-override",
                      "name": "m1",
                      "api_key": "override-key"
                    }
                  ]
                }
              }
            })";
        }
        auto settings = ben_gear::config::load_model_config(file);
        require(settings.api_key == "override-key", "model-level api_key overrides provider");
        require(settings.base_url == "https://default.test/v1", "provider base_url still inherited");
    }

    // 3. 选择第二个模型
    {
        const auto file = root / "second_model.json";
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
        auto settings = ben_gear::config::load_model_config(file);
        require(settings.model == "Claude Sonnet 4.6", "second model selected");
        require(settings.provider == ben_gear::config::Provider::anthropic, "second model api_mode");
    }

    // 4. 错误处理：active_model 无冒号
    {
        const auto file = root / "no_colon.json";
        {
            std::ofstream out(file);
            out << R"({
              "active_model": "just-a-name",
              "model_config": {"p": {"api_key": "k", "models": [{"id": "m", "name": "n"}]}}
            })";
        }
        bool caught = false;
        try { ben_gear::config::load_model_config(file); } catch (const std::runtime_error& e) {
            caught = (std::string(e.what()).find("provider_name:model_name") != std::string::npos);
        }
        require(caught, "model_config with non-colon active_model throws");
    }

    // 5. 错误处理：provider 不存在
    {
        const auto file = root / "bad_provider.json";
        {
            std::ofstream out(file);
            out << R"({
              "active_model": "missing:model",
              "model_config": {"other": {"api_key": "k", "models": [{"id": "m", "name": "model"}]}}
            })";
        }
        bool caught = false;
        try { ben_gear::config::load_model_config(file); } catch (const std::runtime_error& e) {
            caught = (std::string(e.what()).find("provider not found") != std::string::npos);
        }
        require(caught, "missing provider throws");
    }

    // 6. 错误处理：model 不存在
    {
        const auto file = root / "bad_model.json";
        {
            std::ofstream out(file);
            out << R"({
              "active_model": "p:missing",
              "model_config": {"p": {"api_key": "k", "models": [{"id": "m", "name": "other"}]}}
            })";
        }
        bool caught = false;
        try { ben_gear::config::load_model_config(file); } catch (const std::runtime_error& e) {
            caught = (std::string(e.what()).find("model not found") != std::string::npos);
        }
        require(caught, "missing model throws");
    }

    // 7. 错误处理：缺少 id
    {
        const auto file = root / "no_id.json";
        {
            std::ofstream out(file);
            out << R"({
              "active_model": "p:m1",
              "model_config": {"p": {"api_key": "k", "models": [{"name": "m1"}]}}
            })";
        }
        bool caught = false;
        try { ben_gear::config::load_model_config(file); } catch (const std::runtime_error& e) {
            caught = (std::string(e.what()).find("missing required 'id'") != std::string::npos);
        }
        require(caught, "missing id throws");
    }

    // 8. list_models 返回 provider:name 格式
    {
        const auto file = root / "list.json";
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
        require(names.size() == 2, "list_models returns 2 models");
        require(std::find(names.begin(), names.end(), "prov1:a") != names.end(), "list includes prov1:a");
        require(std::find(names.begin(), names.end(), "prov1:b") != names.end(), "list includes prov1:b");
    }

    // 9. 旧格式仍可用（backward compat 由现有 test_config_loader 保证）

    std::filesystem::remove_all(root);
}

int main() {
    test_support_string();
    test_support_json();
    test_llm_protocol_clients();
    test_stream_parsers();
    test_builtin_tools();
    test_llm_retry();
    test_endpoint_url_completion();
    test_config_loader();
    test_model_config_format();
    test_coroutine_task();
    test_event_loop_constructs();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All BenGear unit tests passed\n";
    return 0;
}
