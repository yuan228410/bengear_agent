#include <gtest/gtest.h>
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/anthropic_client.hpp"

struct EndpointUrlTestCase {
    std::string base_url;
    std::string api_url;
    std::string path;
    std::string expected;
};

class EndpointUrlTest : public ::testing::TestWithParam<EndpointUrlTestCase> {};

TEST_P(EndpointUrlTest, Completion) {
    const auto& p = GetParam();
    ben_gear::config::Settings settings;
    settings.base_url = ben_gear::base::container::String(p.base_url.c_str());
    if (!p.api_url.empty()) {
        settings.api_url = ben_gear::base::container::String(p.api_url.c_str());
    }
    EXPECT_EQ(ben_gear::llm::endpoint_url(settings, p.path), p.expected);
}

INSTANTIATE_TEST_SUITE_P(
    EndpointUrl, EndpointUrlTest,
    ::testing::Values(
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com", "", "/v1/chat/completions",
                            "https://oneapi-comate.baidu-int.com/v1/chat/completions"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com", "", "/v1/messages",
                            "https://oneapi-comate.baidu-int.com/v1/messages"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "", "/v1/chat/completions",
                            "https://oneapi-comate.baidu-int.com/v1/chat/completions"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "", "/v1/messages",
                            "https://oneapi-comate.baidu-int.com/v1/messages"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1/", "", "/v1/messages",
                            "https://oneapi-comate.baidu-int.com/v1/messages"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "https://custom.test/custom/path", "/v1/messages",
                            "https://custom.test/custom/path"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "https://oneapi-comate.baidu-int.com/v1", "/v1/chat/completions",
                            "https://oneapi-comate.baidu-int.com/v1/chat/completions"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "https://oneapi-comate.baidu-int.com/v1", "/v1/messages",
                            "https://oneapi-comate.baidu-int.com/v1/messages"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "https://custom.test/v1/chat/completions", "/v1/chat/completions",
                            "https://custom.test/v1/chat/completions"},
        EndpointUrlTestCase{"https://oneapi-comate.baidu-int.com/v1", "https://custom.test/v1/messages", "/v1/messages",
                            "https://custom.test/v1/messages"}
    )
);
