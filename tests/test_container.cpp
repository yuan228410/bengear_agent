#include <gtest/gtest.h>
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace container = ben_gear::base::container;

// ==================== String 测试 ====================

// --- 构造 ---

TEST(String, DefaultConstruction) {
    container::String s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_STREQ(s.c_str(), "");
}

TEST(String, FromCString) {
    container::String s("hello");
    EXPECT_EQ(s.size(), 5u);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(std::string(s.data(), s.size()), "hello");
}

TEST(String, FromCStringWithLen) {
    container::String s("hello world", 5);
    EXPECT_EQ(std::string(s.data(), s.size()), "hello");
}

TEST(String, FromStdString) {
    std::string src = "test";
    container::String s(src);
    EXPECT_EQ(std::string(s.data(), s.size()), "test");
}

TEST(String, FromStringView) {
    container::String s(std::string_view("view"));
    EXPECT_EQ(std::string(s.data(), s.size()), "view");
}

TEST(String, SmallStringSSO) {
    // <= 22 bytes stays SSO
    container::String s("short");
    EXPECT_EQ(s.size(), 5u);
    EXPECT_LE(s.capacity(), 22u);
}

TEST(String, LargeStringHeap) {
    // > 22 bytes uses heap
    container::String s("this is a longer string that exceeds SSO");
    EXPECT_GT(s.size(), 22u);
    EXPECT_GT(s.capacity(), 22u);
}

TEST(String, CopyConstruction) {
    container::String original("copy me");
    container::String copy(original);
    EXPECT_EQ(std::string(copy.data(), copy.size()), "copy me");
    EXPECT_EQ(std::string(original.data(), original.size()), "copy me");
}

TEST(String, MoveConstruction) {
    container::String original("movable");
    container::String moved(std::move(original));
    EXPECT_EQ(std::string(moved.data(), moved.size()), "movable");
    EXPECT_TRUE(original.empty());
}

// --- 赋值 ---

TEST(String, CopyAssignment) {
    container::String a("first");
    container::String b("second");
    a = b;
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
}

TEST(String, MoveAssignment) {
    container::String a("first");
    container::String b("second");
    a = std::move(b);
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
}

TEST(String, AssignCString) {
    container::String s("old");
    s = "new";
    EXPECT_EQ(std::string(s.data(), s.size()), "new");
}

TEST(String, AssignStringView) {
    container::String s("old");
    s = std::string_view("replaced");
    EXPECT_EQ(std::string(s.data(), s.size()), "replaced");
}

TEST(String, AssignStdString) {
    container::String s("old");
    s = std::string("replaced");
    EXPECT_EQ(std::string(s.data(), s.size()), "replaced");
}

// --- 访问 ---

TEST(String, OperatorBracket) {
    container::String s("abcd");
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s[3], 'd');
}

TEST(String, At) {
    container::String s("abcd");
    EXPECT_EQ(s.at(0), 'a');
    EXPECT_EQ(s.at(3), 'd');
}

TEST(String, AtThrowsOutOfRange) {
    container::String s("abcd");
    EXPECT_THROW(s.at(10), std::out_of_range);
}

TEST(String, FrontBack) {
    container::String s("hello");
    EXPECT_EQ(s.front(), 'h');
    EXPECT_EQ(s.back(), 'o');
}

TEST(String, Capacity) {
    container::String s("hello");
    EXPECT_GE(s.capacity(), s.size());
}

// --- 操作 ---

TEST(String, Clear) {
    container::String s("not empty");
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(String, AppendString) {
    container::String s("hello");
    s.append(container::String(" world"));
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, AppendCString) {
    container::String s("hello");
    s.append(" world");
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, AppendChar) {
    container::String s("ab");
    s.append('c');
    EXPECT_EQ(std::string(s.data(), s.size()), "abc");
}

TEST(String, AppendStringView) {
    container::String s("hello");
    s.append(std::string_view(" world"));
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, OperatorPlusEquals) {
    container::String s("hello");
    s += container::String(" world");
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, Swap) {
    container::String a("first");
    container::String b("second");
    a.swap(b);
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
    EXPECT_EQ(std::string(b.data(), b.size()), "first");
}

// --- 子串 ---

TEST(String, SubstrNormal) {
    container::String s("hello world");
    auto sub = s.substr(6);
    EXPECT_EQ(std::string(sub.data(), sub.size()), "world");
}

TEST(String, SubstrWithLen) {
    container::String s("hello world");
    auto sub = s.substr(0, 5);
    EXPECT_EQ(std::string(sub.data(), sub.size()), "hello");
}

TEST(String, SubstrOutOfRange) {
    container::String s("hello");
    EXPECT_THROW(s.substr(100), std::out_of_range);
}

// --- 查找 ---

TEST(String, FindChar) {
    container::String s("hello");
    EXPECT_EQ(s.find('e'), 1u);
    EXPECT_EQ(s.find('z'), container::String::npos);
}

TEST(String, FindCString) {
    container::String s("hello world");
    EXPECT_EQ(s.find("world"), 6u);
    EXPECT_EQ(s.find("xyz"), container::String::npos);
}

TEST(String, FindStringView) {
    container::String s("hello world");
    EXPECT_EQ(s.find(std::string_view("wor")), 6u);
}

TEST(String, FindWithPos) {
    container::String s("abcabc");
    EXPECT_EQ(s.find('a', 1), 3u);
}

// --- 比较 ---

TEST(String, CompareEqual) {
    container::String a("hello");
    container::String b("hello");
    EXPECT_EQ(a.compare(b), 0);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(String, CompareLess) {
    container::String a("abc");
    container::String b("abd");
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= b);
    EXPECT_FALSE(a > b);
}

TEST(String, CompareGreater) {
    container::String a("abd");
    container::String b("abc");
    EXPECT_TRUE(a > b);
    EXPECT_TRUE(a >= b);
    EXPECT_FALSE(a < b);
}

// --- 拼接 ---

TEST(String, OperatorPlus) {
    container::String a("hello");
    container::String b(" world");
    auto result = a + b;
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

TEST(String, OperatorPlusCString) {
    container::String a("hello");
    auto result = a + " world";
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

TEST(String, OperatorPlusCStringLeft) {
    container::String b("world");
    auto result = "hello " + b;
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

// --- 转换 ---

TEST(String, ToStringView) {
    container::String s("hello");
    std::string_view view = s;
    EXPECT_EQ(view, "hello");
}

TEST(String, ToStdString) {
    container::String s("hello");
    std::string std_s = s.to_std_string();
    EXPECT_EQ(std_s, "hello");
}

TEST(String, HashConsistency) {
    container::String s("test");
    std::string_view sv("test");
    EXPECT_EQ(std::hash<container::String>{}(s), std::hash<std::string_view>{}(sv));
}

// --- 边界情况 ---

TEST(String, NullCString) {
    container::String s(nullptr);
    EXPECT_TRUE(s.empty());
}

TEST(String, EmptyCString) {
    container::String s("");
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(String, AppendBeyondSSO) {
    container::String s("short");
    // Append enough to exceed SSO threshold
    s.append(" this is a longer string that exceeds the SSO buffer");
    EXPECT_GT(s.size(), 22u);
    EXPECT_EQ(std::string(s.data(), s.size()),
              "short this is a longer string that exceeds the SSO buffer");
}

// ==================== Map 测试 ====================

// --- 基本操作 ---

TEST(Map, EmptyMap) {
    container::Map<container::String, int> m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
    EXPECT_EQ(m.begin(), m.end());
}

TEST(Map, InsertAndFind) {
    container::Map<container::String, int> m;
    auto [it, inserted] = m.insert({container::String("key"), 42});
    EXPECT_TRUE(inserted);
    EXPECT_EQ(it->second, 42);

    auto found = m.find(container::String("key"));
    EXPECT_NE(found, m.end());
    EXPECT_EQ(found->second, 42);
}

TEST(Map, InsertDuplicateReturnsExisting) {
    container::Map<container::String, int> m;
    m.insert({container::String("key"), 1});
    auto [it, inserted] = m.insert({container::String("key"), 2});
    EXPECT_FALSE(inserted);
    EXPECT_EQ(it->second, 1);
}

TEST(Map, OperatorBracketInsert) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 10;
    EXPECT_EQ(m[container::String("key")], 10);
}

TEST(Map, OperatorBracketExisting) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 10;
    m[container::String("key")] = 20;
    EXPECT_EQ(m[container::String("key")], 20);
}

TEST(Map, AtFound) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 10;
    EXPECT_EQ(m.at(container::String("key")), 10);
}

TEST(Map, AtThrows) {
    container::Map<container::String, int> m;
    EXPECT_THROW(m.at(container::String("missing")), std::out_of_range);
}

TEST(Map, Contains) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 1;
    EXPECT_TRUE(m.contains(container::String("key")));
    EXPECT_FALSE(m.contains(container::String("missing")));
}

TEST(Map, Count) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 1;
    EXPECT_EQ(m.count(container::String("key")), 1u);
    EXPECT_EQ(m.count(container::String("missing")), 0u);
}

// --- 修改器 ---

TEST(Map, EraseByKey) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 1;
    EXPECT_EQ(m.erase(container::String("key")), 1u);
    EXPECT_FALSE(m.contains(container::String("key")));
    EXPECT_EQ(m.size(), 0u);
}

TEST(Map, EraseMissingKey) {
    container::Map<container::String, int> m;
    EXPECT_EQ(m.erase(container::String("missing")), 0u);
}

TEST(Map, Clear) {
    container::Map<container::String, int> m;
    m[container::String("a")] = 1;
    m[container::String("b")] = 2;
    m.clear();
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
}

// --- 迭代器 ---

TEST(Map, IteratorTraversal) {
    container::Map<container::String, int> m;
    m[container::String("a")] = 1;
    m[container::String("b")] = 2;
    m[container::String("c")] = 3;

    int count = 0;
    for (auto it = m.begin(); it != m.end(); ++it) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST(Map, ConstIteratorTraversal) {
    container::Map<container::String, int> m;
    m[container::String("x")] = 10;

    int count = 0;
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        count++;
    }
    EXPECT_EQ(count, 1);
}

TEST(Map, RangeForLoop) {
    container::Map<int, int> m;
    m[1] = 10;
    m[2] = 20;

    int sum = 0;
    for (const auto& [k, v] : m) {
        sum += v;
    }
    EXPECT_EQ(sum, 30);
}

// --- 拷贝/移动 ---

TEST(Map, CopyConstruction) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    container::Map<container::String, int> copy(m);
    EXPECT_EQ(copy.at(container::String("key")), 42);
    EXPECT_EQ(copy.size(), m.size());
}

TEST(Map, MoveConstruction) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    container::Map<container::String, int> moved(std::move(m));
    EXPECT_EQ(moved.at(container::String("key")), 42);
    EXPECT_EQ(m.size(), 0u);
}

// --- 初始化列表 ---

TEST(Map, InitializerList) {
    container::Map<std::string, int> m({
        {"a", 1}, {"b", 2}, {"c", 3}
    });
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m.at("a"), 1);
    EXPECT_EQ(m.at("c"), 3);
}

// --- 比较运算符 ---

TEST(Map, EqualityOperator) {
    container::Map<std::string, int> a;
    a["x"] = 1;
    a["y"] = 2;

    container::Map<std::string, int> b;
    b["x"] = 1;
    b["y"] = 2;

    EXPECT_TRUE(a == b);
}

TEST(Map, InequalityOperator) {
    container::Map<std::string, int> a;
    a["x"] = 1;

    container::Map<std::string, int> b;
    b["x"] = 2;

    EXPECT_TRUE(a != b);
}

// --- 异构查找 ---

TEST(Map, HeterogeneousFindStringView) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    auto it = m.find(std::string_view("key"));
    EXPECT_NE(it, m.end());
    EXPECT_EQ(it->second, 42);
}

TEST(Map, HeterogeneousContainsCString) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    EXPECT_TRUE(m.contains("key"));
    EXPECT_FALSE(m.contains("missing"));
}

TEST(Map, HeterogeneousCountStringView) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    EXPECT_EQ(m.count(std::string_view("key")), 1u);
    EXPECT_EQ(m.count(std::string_view("missing")), 0u);
}

TEST(Map, HeterogeneousEraseStringView) {
    container::Map<container::String, int> m;
    m[container::String("key")] = 42;

    EXPECT_EQ(m.erase(std::string_view("key")), 1u);
    EXPECT_FALSE(m.contains(container::String("key")));
}

// --- 负载因子与 rehash ---

TEST(Map, LoadFactor) {
    container::Map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    EXPECT_GT(m.load_factor(), 0.0f);
    EXPECT_LE(m.load_factor(), m.max_load_factor());
}

TEST(Map, Rehash) {
    container::Map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    m.rehash(100);
    EXPECT_EQ(m.at(1), 10);
    EXPECT_EQ(m.at(2), 20);
}

TEST(Map, Reserve) {
    container::Map<int, int> m;
    m.reserve(100);
    EXPECT_GE(m.bucket_count(), 100u);
}

TEST(Map, HighVolumeInsert) {
    container::Map<int, int> m;
    for (int i = 0; i < 1000; ++i) {
        m[i] = i * 10;
    }
    EXPECT_EQ(m.size(), 1000u);
    EXPECT_EQ(m.at(500), 5000);
}

// --- 已删除槽复用 ---

TEST(Map, EraseAndInsertReuse) {
    container::Map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    m.erase(1);
    m[3] = 30;
    EXPECT_FALSE(m.contains(1));
    EXPECT_TRUE(m.contains(2));
    EXPECT_TRUE(m.contains(3));
}
