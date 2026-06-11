#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/anthropic_client.hpp"

static void test_endpoint_url(const std::string& base_url, const std::string& api_url,
                               const std::string& path, const std::string& expected) {
    ben_gear::config::Settings settings;
    settings.base_url = ben_gear::base::container::String(base_url.c_str());
    if (!api_url.empty()) {
        settings.api_url = ben_gear::base::container::String(api_url.c_str());
    }
    EXPECT_EQ(ben_gear::llm::endpoint_url(settings, path), expected);
}

TEST(EndpointUrl, CompletionBaseOnly) {
    test_endpoint_url("https://oneapi-comate.baidu-int.com", "", "/v1/chat/completions",
                      "https://oneapi-comate.baidu-int.com/v1/chat/completions");
    test_endpoint_url("https://oneapi-comate.baidu-int.com", "", "/v1/messages",
                      "https://oneapi-comate.baidu-int.com/v1/messages");
}

TEST(EndpointUrl, CompletionBaseWithV1) {
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "", "/v1/chat/completions",
                      "https://oneapi-comate.baidu-int.com/v1/chat/completions");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "", "/v1/messages",
                      "https://oneapi-comate.baidu-int.com/v1/messages");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1/", "", "/v1/messages",
                      "https://oneapi-comate.baidu-int.com/v1/messages");
}

TEST(EndpointUrl, CustomApiUrl) {
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "https://custom.test/custom/path", "/v1/messages",
                      "https://custom.test/custom/path");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "https://oneapi-comate.baidu-int.com/v1", "/v1/chat/completions",
                      "https://oneapi-comate.baidu-int.com/v1/chat/completions");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "https://oneapi-comate.baidu-int.com/v1", "/v1/messages",
                      "https://oneapi-comate.baidu-int.com/v1/messages");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "https://custom.test/v1/chat/completions", "/v1/chat/completions",
                      "https://custom.test/v1/chat/completions");
    test_endpoint_url("https://oneapi-comate.baidu-int.com/v1", "https://custom.test/v1/messages", "/v1/messages",
                      "https://custom.test/v1/messages");
}
