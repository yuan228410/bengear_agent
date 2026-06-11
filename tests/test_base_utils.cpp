#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/base/utils/json.hpp"

TEST(StringUtils, Trim) {
    EXPECT_EQ(ben_gear::base::utils::trim("  BenGear\n"), "BenGear");
}

TEST(StringUtils, ToLower) {
    EXPECT_EQ(ben_gear::base::utils::to_lower("OpenAI"), "openai");
}

TEST(JsonUtils, JsonStringEscapes) {
    EXPECT_EQ(ben_gear::json_string("a\"b\n"), "\"a\\\"b\\n\"");
}

TEST(JsonUtils, ParseAndExtract) {
    std::string error;
    auto json = ben_gear::parse_json("{\"text\":\"hello\\nworld\"}", error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(ben_gear::get_json_value<std::string>(json, "text"), "hello\nworld");
}
