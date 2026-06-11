#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/base/json/json.hpp"
#include "ben_gear/base/utils/json.hpp"

using namespace ben_gear::base::container;

// ==================== 基础构造 ====================

TEST(JsonTest, NullDefault) {
    Json j;
    EXPECT_TRUE(j.is_null());
    EXPECT_TRUE(j.empty());
    EXPECT_EQ(j.size(), 0u);
}

TEST(JsonTest, BoolConstruct) {
    Json j(true);
    EXPECT_TRUE(j.is_bool());
    EXPECT_TRUE(j.as_bool());
}

TEST(JsonTest, IntConstruct) {
    Json j(42);
    EXPECT_TRUE(j.is_number());
    EXPECT_EQ(j.as_int(), 42);
}

TEST(JsonTest, DoubleConstruct) {
    Json j(3.14);
    EXPECT_TRUE(j.is_number_float());
    EXPECT_DOUBLE_EQ(j.as_double(), 3.14);
}

TEST(JsonTest, StringConstruct) {
    Json j("hello");
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.as_string(), "hello");
}

TEST(JsonTest, ContainerStringConstruct) {
    String s = "world";
    Json j(s);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.as_string(), "world");
}

// ==================== 数组 ====================

TEST(JsonTest, ArrayFactory) {
    auto j = Json::array({1, 2, 3});
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 3u);
    EXPECT_EQ(j[0].as_int(), 1);
    EXPECT_EQ(j[1].as_int(), 2);
    EXPECT_EQ(j[2].as_int(), 3);
}

TEST(JsonTest, ArrayPushBack) {
    auto j = Json::array();
    j.push_back(10);
    j.push_back(20);
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j[0].as_int(), 10);
}

TEST(JsonTest, ArrayEmpty) {
    auto j = Json::array();
    EXPECT_TRUE(j.is_array());
    EXPECT_TRUE(j.empty());
}

// ==================== 对象 ====================

TEST(JsonTest, ObjectFactory) {
    auto j = Json::object();
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.empty());
}

TEST(JsonTest, ObjectInitList) {
    Json j = {{"name", "test"}, {"value", 42}};
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j["name"].get<std::string>(), "test");
    EXPECT_EQ(j["value"].as_int(), 42);
}

TEST(JsonTest, ObjectSetGet) {
    Json j = Json::object();
    j["key1"] = "val1";
    j["key2"] = 100;
    EXPECT_TRUE(j.contains("key1"));
    EXPECT_EQ(j["key1"].get<std::string>(), "val1");
    EXPECT_EQ(j["key2"].as_int(), 100);
}

TEST(JsonTest, ObjectContains) {
    Json j = {{"a", 1}};
    EXPECT_TRUE(j.contains("a"));
    EXPECT_FALSE(j.contains("b"));
}

TEST(JsonTest, ObjectErase) {
    Json j = {{"a", 1}, {"b", 2}};
    EXPECT_EQ(j.erase("a"), 1u);
    EXPECT_FALSE(j.contains("a"));
    EXPECT_TRUE(j.contains("b"));
}

// ==================== ProxyRef 链式调用 ====================

TEST(JsonTest, ProxyRefChainWrite) {
    Json j = Json::object();
    j["progress"]["completed"] = 5;
    j["progress"]["total"] = 10;
    EXPECT_TRUE(j["progress"].is_object());
    EXPECT_EQ(j["progress"]["completed"].as_int(), 5);
    EXPECT_EQ(j["progress"]["total"].as_int(), 10);
}

TEST(JsonTest, ProxyRefChainRead) {
    Json j = {{"choices", {{{"message", {{"content", "hi"}}}}}}};
    Json choices = j["choices"];
    Json msg = choices[0]["message"];
    EXPECT_EQ(msg["content"].get<std::string>(), "hi");
}

TEST(JsonTest, ProxyRefArrayAccess) {
    Json j = {{"arr", {1, 2, 3}}};
    EXPECT_EQ(j["arr"][0].as_int(), 1);
    EXPECT_EQ(j["arr"][2].as_int(), 3);
}

TEST(JsonTest, ProxyRefAssignTypes) {
    Json j = Json::object();
    j["b"] = true;
    j["i"] = 42;
    j["d"] = 3.14;
    j["s"] = "str";
    j["n"] = nullptr;
    EXPECT_TRUE(j["b"].as_bool());
    EXPECT_EQ(j["i"].as_int(), 42);
    EXPECT_DOUBLE_EQ(j["d"].as_double(), 3.14);
    EXPECT_EQ(j["s"].as_string(), "str");
    EXPECT_TRUE(j["n"].is_null());
}

// ==================== 解析 ====================

TEST(JsonTest, ParseSimple) {
    auto j = Json::parse(R"({"key": "value", "num": 42})");
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["key"].get<std::string>(), "value");
    EXPECT_EQ(j["num"].as_int(), 42);
}

TEST(JsonTest, ParseArray) {
    auto j = Json::parse(R"([1, "two", 3.0, true, null])");
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 5u);
    EXPECT_EQ(j[0].as_int(), 1);
    EXPECT_EQ(j[1].get<std::string>(), "two");
    EXPECT_DOUBLE_EQ(j[2].as_double(), 3.0);
    EXPECT_TRUE(j[3].as_bool());
    EXPECT_TRUE(j[4].is_null());
}

TEST(JsonTest, ParseNested) {
    auto j = Json::parse(R"({"a": {"b": {"c": 99}}})");
    EXPECT_EQ(j["a"]["b"]["c"].as_int(), 99);
}

TEST(JsonTest, ParseError) {
    ben_gear::base::container::String err;
    auto j = Json::parse("{invalid", err);
    EXPECT_FALSE(err.empty());
}

// ==================== 序列化 ====================

TEST(JsonTest, DumpCompact) {
    Json j = {{"a", 1}};
    auto s = j.dump();
    EXPECT_FALSE(s.empty());
    // 应包含 a 和 1
    EXPECT_NE(s.find('a'), std::string::npos);
}

TEST(JsonTest, DumpPretty) {
    Json j = {{"a", 1}};
    auto s = j.dump(2);
    EXPECT_NE(std::string(s.data(), s.size()).find('\n'), std::string::npos);
}

// ==================== 迭代器 ====================

TEST(JsonTest, ObjectIterator) {
    Json j = {{"x", 1}, {"y", 2}};
    int count = 0;
    for (auto it = j.begin(); it != j.end(); ++it) {
        count++;
        EXPECT_FALSE(it.key().empty());
    }
    EXPECT_EQ(count, 2);
}

TEST(JsonTest, ArrayIterator) {
    Json j = {10, 20, 30};
    int sum = 0;
    for (const auto& el : j) {
        sum += static_cast<int>(el.as_int());
    }
    EXPECT_EQ(sum, 60);
}

TEST(JsonTest, FindKey) {
    Json j = {{"a", 1}, {"b", 2}};
    auto it = j.find("b");
    EXPECT_NE(it, j.end());
    EXPECT_EQ(it->as_int(), 2);
}

TEST(JsonTest, FindMissing) {
    Json j = {{"a", 1}};
    auto it = j.find("z");
    EXPECT_EQ(it, j.end());
}

// ==================== 类型判断 ====================

TEST(JsonTest, TypeNames) {
    EXPECT_STREQ(Json().type_name(), "null");
    EXPECT_STREQ(Json(true).type_name(), "boolean");
    EXPECT_STREQ(Json(42).type_name(), "number");
    EXPECT_STREQ(Json("s").type_name(), "string");
    EXPECT_STREQ(Json::array().type_name(), "array");
    EXPECT_STREQ(Json::object().type_name(), "object");
}

TEST(JsonTest, IsNumberVariants) {
    Json ji(42);
    Json ju(uint64_t(42));
    Json jd(3.14);
    EXPECT_TRUE(ji.is_number_integer());
    EXPECT_TRUE(ju.is_number_unsigned());
    EXPECT_TRUE(jd.is_number_float());
}

// ==================== get<T> ====================

TEST(JsonTest, GetTypes) {
    Json j = {{"b", true}, {"i", -1}, {"u", uint64_t(42)}, {"d", 2.5}, {"s", "hi"}};
    EXPECT_EQ(j["b"].get<bool>(), true);
    EXPECT_EQ(j["i"].get<int>(), -1);
    EXPECT_EQ(j["u"].get<uint64_t>(), 42u);
    EXPECT_DOUBLE_EQ(j["d"].get<double>(), 2.5);
    EXPECT_EQ(j["s"].get<std::string>(), "hi");
}

// ==================== value() ====================

TEST(JsonTest, ValueWithDefault) {
    Json j = {{"x", 10}};
    EXPECT_EQ(j.value("x", 0), 10);
    EXPECT_EQ(j.value("missing", 99), 99);
}

// ==================== 比较 ====================

TEST(JsonTest, Equality) {
    EXPECT_EQ(Json(42), Json(42));
    EXPECT_EQ(Json("abc"), Json("abc"));
    EXPECT_NE(Json(1), Json(2));
    EXPECT_EQ(Json(), Json());
}

// ==================== 工具函数 ====================

TEST(JsonTest, ParseJsonUtil) {
    auto j = ben_gear::parse_json(R"({"ok": true})");
    EXPECT_TRUE(j["ok"].as_bool());
}

TEST(JsonTest, GetJsonValue) {
    Json j = {{"name", "test"}, {"count", 5}};
    auto name = ben_gear::get_json_value<std::string>(j, "name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "test");

    auto missing = ben_gear::get_json_value<std::string>(j, "missing");
    EXPECT_FALSE(missing.has_value());
}

// ==================== items() ====================

TEST(JsonTest, ItemsIteration) {
    Json j = {{"a", 1}, {"b", 2}};
    int count = 0;
    for (auto it = j.items().begin(); it != j.items().end(); ++it) {
        count++;
    }
    EXPECT_EQ(count, 2);
}

// ==================== ProxyRef 迭代器安全性 ====================

TEST(JsonTest, ProxyRefIteratorArrayNotDangling) {
    // 核心场景：for (auto x : json["key"]) 不应产生悬空迭代器
    Json j = {{"items", {1, 2, 3}}};
    int sum = 0;
    for (auto item : j["items"]) {
        sum += item.as_int();
    }
    EXPECT_EQ(sum, 6);
}

TEST(JsonTest, ProxyRefIteratorObjectNotDangling) {
    Json j = {{"props", {{"a", 1}, {"b", 2}}}};
    int sum = 0;
    for (auto entry : j["props"]) {
        sum += entry.as_int();
    }
    EXPECT_EQ(sum, 3);
}

TEST(JsonTest, ProxyRefIteratorNestedArray) {
    // 模拟 OpenAI tool_calls 响应结构
    Json j = Json::parse(R"({
        "choices": [{
            "delta": {
                "tool_calls": [{
                    "index": 0,
                    "id": "call_123",
                    "function": {
                        "name": "http_get",
                        "arguments": "{\"url\":\"https://example.com\"}"
                    }
                }]
            }
        }]
    })");

    auto choices = j["choices"];
    auto first_choice = choices[0];
    auto delta = first_choice["delta"];
    auto tool_calls = delta["tool_calls"];

    // 关键：for-range on ProxyRef 不应读到 \0 垃圾
    for (auto tc : tool_calls) {
        EXPECT_EQ(tc["id"].get<std::string>(), "call_123");
        EXPECT_EQ(tc["function"]["name"].get<std::string>(), "http_get");
        EXPECT_EQ(tc["function"]["arguments"].get<std::string>(), "{\"url\":\"https://example.com\"}");
    }
}

TEST(JsonTest, ProxyRefFindNotDangling) {
    Json j = {{"data", {{"key1", "val1"}, {"key2", "val2"}}}};
    auto it = j["data"].find("key2");
    ASSERT_NE(it, j["data"].end());
    EXPECT_EQ(it.value().get<std::string>(), "val2");
}
