#include "test_framework.hpp"
#include <unordered_map>

#include <string>
#include <string_view>
#include <random>
#include <vector>

namespace container = ben_gear::base::container;

// ==================== String 测试 ====================

// --- 构造 ---

TEST(String, DefaultConstruction) {
    std::string s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_STREQ(s.c_str(), "");
}

TEST(String, FromCString) {
    std::string s("hello");
    EXPECT_EQ(s.size(), 5u);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(std::string(s.data(), s.size()), "hello");
}

TEST(String, FromCStringWithLen) {
    std::string s("hello world", 5);
    EXPECT_EQ(std::string(s.data(), s.size()), "hello");
}

TEST(String, FromStdString) {
    std::string src = "test";
    std::string s(src);
    EXPECT_EQ(std::string(s.data(), s.size()), "test");
}

TEST(String, FromStringView) {
    std::string s(std::string_view("view"));
    EXPECT_EQ(std::string(s.data(), s.size()), "view");
}

TEST(String, SmallStringSSO) {
    std::string s("short");
    EXPECT_EQ(s.size(), 5u);
}

TEST(String, LargeStringHeap) {
    std::string s("this is a longer string that exceeds SSO");
    EXPECT_GT(s.size(), 15u);
}

TEST(String, CopyConstruction) {
    std::string original("copy me");
    std::string copy(original);
    EXPECT_EQ(std::string(copy.data(), copy.size()), "copy me");
    EXPECT_EQ(std::string(original.data(), original.size()), "copy me");
}

TEST(String, MoveConstruction) {
    std::string original("movable");
    std::string moved(std::move(original));
    EXPECT_EQ(std::string(moved.data(), moved.size()), "movable");
    EXPECT_TRUE(original.empty());
}

// --- 赋值 ---

TEST(String, CopyAssignment) {
    std::string a("first");
    std::string b("second");
    a = b;
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
}

TEST(String, MoveAssignment) {
    std::string a("first");
    std::string b("second");
    a = std::move(b);
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
}

TEST(String, AssignCString) {
    std::string s("old");
    s = "new";
    EXPECT_EQ(std::string(s.data(), s.size()), "new");
}

TEST(String, AssignStringView) {
    std::string s("old");
    s = std::string_view("replaced");
    EXPECT_EQ(std::string(s.data(), s.size()), "replaced");
}

TEST(String, AssignStdString) {
    std::string s("old");
    s = std::string("replaced");
    EXPECT_EQ(std::string(s.data(), s.size()), "replaced");
}

// --- 访问 ---

TEST(String, OperatorBracket) {
    std::string s("abcd");
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s[3], 'd');
}

TEST(String, At) {
    std::string s("abcd");
    EXPECT_EQ(s.at(0), 'a');
    EXPECT_EQ(s.at(3), 'd');
}

TEST(String, AtThrowsOutOfRange) {
    std::string s("abcd");
    EXPECT_THROW(s.at(10), std::out_of_range);
}

TEST(String, FrontBack) {
    std::string s("hello");
    EXPECT_EQ(s.front(), 'h');
    EXPECT_EQ(s.back(), 'o');
}

TEST(String, Capacity) {
    std::string s("hello");
    EXPECT_GE(s.capacity(), s.size());
}

// --- 操作 ---

TEST(String, Clear) {
    std::string s("not empty");
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(String, AppendString) {
    std::string s("hello");
    s.append(std::string(" world"));
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, AppendCString) {
    std::string s("hello");
    s.append(" world");
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, AppendChar) {
    std::string s("ab");
    s += 'c';
    EXPECT_EQ(std::string(s.data(), s.size()), "abc");
}

TEST(String, AppendStringView) {
    std::string s("hello");
    s.append(std::string_view(" world"));
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, OperatorPlusEquals) {
    std::string s("hello");
    s += std::string(" world");
    EXPECT_EQ(std::string(s.data(), s.size()), "hello world");
}

TEST(String, Swap) {
    std::string a("first");
    std::string b("second");
    a.swap(b);
    EXPECT_EQ(std::string(a.data(), a.size()), "second");
    EXPECT_EQ(std::string(b.data(), b.size()), "first");
}

// --- 子串 ---

TEST(String, SubstrNormal) {
    std::string s("hello world");
    auto sub = s.substr(6);
    EXPECT_EQ(std::string(sub.data(), sub.size()), "world");
}

TEST(String, SubstrWithLen) {
    std::string s("hello world");
    auto sub = s.substr(0, 5);
    EXPECT_EQ(std::string(sub.data(), sub.size()), "hello");
}

TEST(String, SubstrOutOfRange) {
    std::string s("hello");
    EXPECT_THROW(s.substr(100), std::out_of_range);
}

// --- 查找 ---

TEST(String, FindChar) {
    std::string s("hello");
    EXPECT_EQ(s.find('e'), 1u);
    EXPECT_EQ(s.find('z'), std::string::npos);
}

TEST(String, FindCString) {
    std::string s("hello world");
    EXPECT_EQ(s.find("world"), 6u);
    EXPECT_EQ(s.find("xyz"), std::string::npos);
}

TEST(String, FindStringView) {
    std::string s("hello world");
    EXPECT_EQ(s.find(std::string_view("wor")), 6u);
}

TEST(String, FindWithPos) {
    std::string s("abcabc");
    EXPECT_EQ(s.find('a', 1), 3u);
}

TEST(String, FindPastEndReturnsNpos) {
    std::string s("abc");
    EXPECT_EQ(s.find('a', 4), std::string::npos);
    EXPECT_EQ(s.find("a", 4), std::string::npos);
    EXPECT_EQ(s.find("", 4), std::string::npos);
    EXPECT_EQ(s.find("", 3), 3u);
}

TEST(String, ReserveDoesNotShrinkLargeString) {
    std::string s("this is a longer string that exceeds SSO");
    auto original = std::string(s.data(), s.size());
    auto old_capacity = s.capacity();
    s.reserve(1);
    EXPECT_EQ(std::string(s.data(), s.size()), original);
    EXPECT_EQ(s.capacity(), old_capacity);
}

// --- 比较 ---

TEST(String, CompareEqual) {
    std::string a("hello");
    std::string b("hello");
    EXPECT_EQ(a.compare(b), 0);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(String, CompareLess) {
    std::string a("abc");
    std::string b("abd");
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= b);
    EXPECT_FALSE(a > b);
}

TEST(String, CompareGreater) {
    std::string a("abd");
    std::string b("abc");
    EXPECT_TRUE(a > b);
    EXPECT_TRUE(a >= b);
    EXPECT_FALSE(a < b);
}

// --- 拼接 ---

TEST(String, OperatorPlus) {
    std::string a("hello");
    std::string b(" world");
    auto result = a + b;
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

TEST(String, OperatorPlusCString) {
    std::string a("hello");
    auto result = a + " world";
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

TEST(String, OperatorPlusCStringLeft) {
    std::string b("world");
    auto result = "hello " + b;
    EXPECT_EQ(std::string(result.data(), result.size()), "hello world");
}

// --- 转换 ---

TEST(String, ToStringView) {
    std::string s("hello");
    std::string_view view = s;
    EXPECT_EQ(view, "hello");
}

TEST(String, ToStdString) {
    std::string s("hello");
    std::string std_s = s;
    EXPECT_EQ(std_s, "hello");
}

TEST(String, HashConsistency) {
    std::string s("test");
    std::string_view sv("test");
    EXPECT_EQ(std::hash<std::string>{}(s), std::hash<std::string_view>{}(sv));
}

// --- 边界情况 ---

TEST(String, NullCString) {
    std::string s(nullptr);
    EXPECT_TRUE(s.empty());
}

TEST(String, EmptyCString) {
    std::string s("");
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(String, AppendBeyondSSO) {
    std::string s("short");
    // Append enough to exceed SSO threshold
    s.append(" this is a longer string that exceeds the SSO buffer");
    EXPECT_GT(s.size(), 22u);
    EXPECT_EQ(std::string(s.data(), s.size()),
              "short this is a longer string that exceeds the SSO buffer");
}

// ==================== Map 测试 ====================

// --- 基本操作 ---

TEST(Map, EmptyMap) {
    std::unordered_map<std::string, int> m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
    EXPECT_EQ(m.begin(), m.end());
}

TEST(Map, InsertAndFind) {
    std::unordered_map<std::string, int> m;
    auto [it, inserted] = m.insert({std::string("key"), 42});
    EXPECT_TRUE(inserted);
    EXPECT_EQ(it->second, 42);

    auto found = m.find(std::string("key"));
    EXPECT_NE(found, m.end());
    EXPECT_EQ(found->second, 42);
}

TEST(Map, InsertDuplicateReturnsExisting) {
    std::unordered_map<std::string, int> m;
    m.insert({std::string("key"), 1});
    auto [it, inserted] = m.insert({std::string("key"), 2});
    EXPECT_FALSE(inserted);
    EXPECT_EQ(it->second, 1);
}

TEST(Map, OperatorBracketInsert) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 10;
    EXPECT_EQ(m[std::string("key")], 10);
}

TEST(Map, OperatorBracketExisting) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 10;
    m[std::string("key")] = 20;
    EXPECT_EQ(m[std::string("key")], 20);
}

TEST(Map, AtFound) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 10;
    EXPECT_EQ(m.at(std::string("key")), 10);
}

TEST(Map, AtThrows) {
    std::unordered_map<std::string, int> m;
    EXPECT_THROW(m.at(std::string("missing")), std::out_of_range);
}

TEST(Map, Contains) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 1;
    EXPECT_TRUE(m.contains(std::string("key")));
    EXPECT_FALSE(m.contains(std::string("missing")));
}

TEST(Map, Count) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 1;
    EXPECT_EQ(m.count(std::string("key")), 1u);
    EXPECT_EQ(m.count(std::string("missing")), 0u);
}

// --- 修改器 ---

TEST(Map, EraseByKey) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 1;
    EXPECT_EQ(m.erase(std::string("key")), 1u);
    EXPECT_FALSE(m.contains(std::string("key")));
    EXPECT_EQ(m.size(), 0u);
}

TEST(Map, EraseMissingKey) {
    std::unordered_map<std::string, int> m;
    EXPECT_EQ(m.erase(std::string("missing")), 0u);
}

TEST(Map, Clear) {
    std::unordered_map<std::string, int> m;
    m[std::string("a")] = 1;
    m[std::string("b")] = 2;
    m.clear();
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
}

// --- 迭代器 ---

TEST(Map, IteratorTraversal) {
    std::unordered_map<std::string, int> m;
    m[std::string("a")] = 1;
    m[std::string("b")] = 2;
    m[std::string("c")] = 3;

    int count = 0;
    for (auto it = m.begin(); it != m.end(); ++it) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST(Map, ConstIteratorTraversal) {
    std::unordered_map<std::string, int> m;
    m[std::string("x")] = 10;

    int count = 0;
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        count++;
    }
    EXPECT_EQ(count, 1);
}

TEST(Map, RangeForLoop) {
    std::unordered_map<int, int> m;
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
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    std::unordered_map<std::string, int> copy(m);
    EXPECT_EQ(copy.at(std::string("key")), 42);
    EXPECT_EQ(copy.size(), m.size());
}

TEST(Map, MoveConstruction) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    std::unordered_map<std::string, int> moved(std::move(m));
    EXPECT_EQ(moved.at(std::string("key")), 42);
    EXPECT_EQ(m.size(), 0u);
}

// --- 初始化列表 ---

TEST(Map, InitializerList) {
    std::unordered_map<std::string, int> m({
        {"a", 1}, {"b", 2}, {"c", 3}
    });
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m.at("a"), 1);
    EXPECT_EQ(m.at("c"), 3);
}

// --- 比较运算符 ---

TEST(Map, EqualityOperator) {
    std::unordered_map<std::string, int> a;
    a["x"] = 1;
    a["y"] = 2;

    std::unordered_map<std::string, int> b;
    b["x"] = 1;
    b["y"] = 2;

    EXPECT_TRUE(a == b);
}

TEST(Map, InequalityOperator) {
    std::unordered_map<std::string, int> a;
    a["x"] = 1;

    std::unordered_map<std::string, int> b;
    b["x"] = 2;

    EXPECT_TRUE(a != b);
}

// --- 异构查找 ---

TEST(Map, HeterogeneousFindStringView) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    auto it = m.find(std::string("key"));
    EXPECT_NE(it, m.end());
    EXPECT_EQ(it->second, 42);
}

TEST(Map, HeterogeneousContainsCString) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    EXPECT_TRUE(m.contains("key"));
    EXPECT_FALSE(m.contains("missing"));
}

TEST(Map, HeterogeneousCountStringView) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    EXPECT_EQ(m.count(std::string("key")), 1u);
    EXPECT_EQ(m.count(std::string("missing")), 0u);
}

TEST(Map, HeterogeneousEraseStringView) {
    std::unordered_map<std::string, int> m;
    m[std::string("key")] = 42;

    EXPECT_EQ(m.erase(std::string("key")), 1u);
    EXPECT_FALSE(m.contains(std::string("key")));
}

// --- 负载因子与 rehash ---

TEST(Map, LoadFactor) {
    std::unordered_map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    EXPECT_GT(m.load_factor(), 0.0f);
    EXPECT_LE(m.load_factor(), m.max_load_factor());
}

TEST(Map, Rehash) {
    std::unordered_map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    m.rehash(100);
    EXPECT_EQ(m.at(1), 10);
    EXPECT_EQ(m.at(2), 20);
}

TEST(Map, Reserve) {
    std::unordered_map<int, int> m;
    m.reserve(100);
    EXPECT_GE(m.bucket_count(), 100u);
}

TEST(Map, HighVolumeInsert) {
    std::unordered_map<int, int> m;
    for (int i = 0; i < 1000; ++i) {
        m[i] = i * 10;
    }
    EXPECT_EQ(m.size(), 1000u);
    EXPECT_EQ(m.at(500), 5000);
}

// --- 已删除槽复用 ---

TEST(Map, EraseAndInsertReuse) {
    std::unordered_map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    m.erase(1);
    m[3] = 30;
    EXPECT_FALSE(m.contains(1));
    EXPECT_TRUE(m.contains(2));
    EXPECT_TRUE(m.contains(3));
}


TEST(Map, RandomizedOperationsMatchStdUnorderedMap) {
    std::unordered_map<int, int> m;
    std::unordered_map<int, int> ref;
    std::mt19937 rng(1234567);

    for (int step = 0; step < 5000; ++step) {
        int key = static_cast<int>(rng() % 257) - 128;
        int value = static_cast<int>(rng() % 100000);
        int op = static_cast<int>(rng() % 5);

        switch (op) {
        case 0:
        case 1:
            m[key] = value;
            ref[key] = value;
            break;
        case 2: {
            auto erased = m.erase(key);
            auto ref_erased = ref.erase(key);
            EXPECT_EQ(erased, ref_erased);
            break;
        }
        case 3:
            m.reserve(static_cast<std::size_t>((rng() % 400) + 1));
            break;
        case 4:
            m.rehash(static_cast<std::size_t>((rng() % 400) + 1));
            break;
        }

        EXPECT_EQ(m.size(), ref.size());
        for (int probe = -140; probe <= 140; probe += 7) {
            EXPECT_EQ(m.contains(probe), ref.find(probe) != ref.end());
            if (auto it = ref.find(probe); it != ref.end()) {
                EXPECT_EQ(m.at(probe), it->second);
            }
        }
    }

    std::size_t iter_count = 0;
    for (const auto& [k, v] : m) {
        auto it = ref.find(k);
        EXPECT_TRUE(it != ref.end());
        if (it != ref.end()) EXPECT_EQ(v, it->second);
        ++iter_count;
    }
    EXPECT_EQ(iter_count, ref.size());
}

TEST(Map, ClearThenReuseAfterManyDeletes) {
    std::unordered_map<int, int> m;
    for (int i = 0; i < 200; ++i) m[i] = i;
    for (int i = 0; i < 200; i += 2) EXPECT_EQ(m.erase(i), 1u);
    m.clear();
    EXPECT_TRUE(m.empty());
    for (int i = 0; i < 200; ++i) m[i + 1000] = i * 3;
    EXPECT_EQ(m.size(), 200u);
    EXPECT_EQ(m.at(1000), 0);
    EXPECT_EQ(m.at(1199), 597);
}
