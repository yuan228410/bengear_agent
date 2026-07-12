#pragma once

/// BenGear 轻量测试框架
///
/// 与 gtest 宏签名兼容，测试文件改动最小。
/// 支持：TEST, EXPECT_*, ASSERT_*, --filter, --verbose

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <typeinfo>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ben_gear::test {

// ==================== 测试注册 ====================

struct TestInfo {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

namespace detail {

inline std::vector<TestInfo>& tests() {
    static std::vector<TestInfo> t;
    return t;
}

inline std::mutex& tests_mutex() {
    static std::mutex m;
    return m;
}

inline bool& verbose() {
    static bool v = false;
    return v;
}

inline std::string& current_suite() {
    static std::string s;
    return s;
}

inline std::string& current_test() {
    static std::string t;
    return t;
}

inline int& fail_count() {
    static int c = 0;
    return c;
}

inline int& current_fail_count() {
    static int c = 0;
    return c;
}

inline void report_failure(const char* file, int line, const std::string& msg) {
    ++fail_count();
    ++current_fail_count();
    std::fprintf(stderr, "  FAIL  %s:%d: %s\n", file, line, msg.c_str());
}

}  // namespace detail

struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, void(*fn)()) {
        std::lock_guard lock(detail::tests_mutex());
        detail::tests().push_back({suite, name, fn});
    }
};

// ==================== 断言宏 ====================

#define BEN_GEAR_TEST_ASSERT_IMPL_(file, line, expr, msg)          \
    do {                                                           \
        if (!(expr)) {                                             \
            ::ben_gear::test::detail::report_failure(              \
                file, line, msg);                                  \
        }                                                          \
    } while (0)

#define BEN_GEAR_TEST_ASSERT_FATAL_(file, line, expr, msg)         \
    do {                                                           \
        if (!(expr)) {                                             \
            ::ben_gear::test::detail::report_failure(              \
                file, line, msg);                                  \
            return;                                                \
        }                                                          \
    } while (0)

// EXPECT_*
#define EXPECT_TRUE(x)    BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (x), "EXPECT_TRUE(" #x ")")
#define EXPECT_FALSE(x)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, !(x), "EXPECT_FALSE(" #x ")")
#define EXPECT_EQ(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) == (b), "EXPECT_EQ(" #a ", " #b ")")
#define EXPECT_NE(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) != (b), "EXPECT_NE(" #a ", " #b ")")
#define EXPECT_LT(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) < (b),  "EXPECT_LT(" #a ", " #b ")")
#define EXPECT_LE(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) <= (b), "EXPECT_LE(" #a ", " #b ")")
#define EXPECT_GT(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) > (b),  "EXPECT_GT(" #a ", " #b ")")
#define EXPECT_GE(a, b)   BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, (a) >= (b), "EXPECT_GE(" #a ", " #b ")")

#define EXPECT_STREQ(a, b) BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, \
    std::strcmp((a), (b)) == 0, "EXPECT_STREQ(" #a ", " #b ")")

#define EXPECT_NEAR(a, b, tol) BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, \
    std::abs((a) - (b)) <= (tol), "EXPECT_NEAR(" #a ", " #b ", " #tol ")")

#define EXPECT_THROW(expr, exc_type)                                        \
    do {                                                                    \
        bool caught_ = false;                                               \
        try { expr; } catch (const exc_type&) { caught_ = true; }           \
        BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, caught_,            \
            "EXPECT_THROW(" #expr ", " #exc_type ")");                     \
    } while (0)

#define EXPECT_NO_THROW(expr)                                               \
    do {                                                                    \
        bool caught_ = false;                                               \
        try { expr; } catch (const std::exception&) { caught_ = true; }     \
        BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, !caught_,           \
            "EXPECT_NO_THROW(" #expr ")");                                  \
    } while (0)

#define EXPECT_ANY_THROW(expr)                                              \
    do {                                                                    \
        bool caught_ = false;                                               \
        try { expr; } catch (...) { caught_ = true; }                       \
        BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__, caught_,            \
            "EXPECT_ANY_THROW(" #expr ")");                                 \
    } while (0)

// ASSERT_*（失败后 return）
#define ASSERT_TRUE(x)   BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (x), "ASSERT_TRUE(" #x ")")
#define ASSERT_FALSE(x)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, !(x), "ASSERT_FALSE(" #x ")")
#define ASSERT_EQ(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) == (b), "ASSERT_EQ(" #a ", " #b ")")
#define ASSERT_NE(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) != (b), "ASSERT_NE(" #a ", " #b ")")

// ==================== TEST 宏 ====================

#define TEST(suite, name)                                                    \
    void ben_gear_test_##suite##_##name();                                   \
    static ::ben_gear::test::TestRegistrar                                   \
        ben_gear_reg_##suite##_##name(                                       \
            #suite, #name, ben_gear_test_##suite##_##name);                  \
    void ben_gear_test_##suite##_##name()

// ==================== 异常/崩溃诊断 ====================

inline const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (段错误/非法内存访问)";
        case SIGABRT: return "SIGABRT (程序异常终止)";
        case SIGFPE:  return "SIGFPE (算术异常)";
        case SIGILL:  return "SIGILL (非法指令)";
#ifndef _WIN32
        case SIGBUS:  return "SIGBUS (总线错误)";
#endif
        default:      return "未知信号";
    }
}

inline void crash_handler(int sig) {
    // 防止 abort() 触发的二次信号
    static bool in_handler = false;
    if (in_handler) std::_Exit(1);
    in_handler = true;

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "[CRASH] 收到信号 %s (%d)\n", signal_name(sig), sig);
    std::fprintf(stderr, "  正在执行测试: %s.%s\n",
                 detail::current_suite().c_str(),
                 detail::current_test().c_str());
    std::fprintf(stderr, "========================================\n");
    std::fflush(stderr);
    std::_Exit(1);
}

// Windows 结构化异常 → 诊断输出
#ifdef _WIN32
struct CrashContext {
    const char* suite;
    const char* test;
};
inline thread_local CrashContext t_crash_ctx_{nullptr, nullptr};

inline void install_seh_handler() {
    // SetUnhandledExceptionFilter 对 MinGW 和 MSVC 都可用
    SetUnhandledExceptionFilter([](_EXCEPTION_POINTERS* ep) -> LONG {
        std::fprintf(stderr, "\n========================================\n");
        std::fprintf(stderr, "[CRASH] Windows 结构化异常 0x%08X\n",
                     ep->ExceptionRecord->ExceptionCode);
        if (t_crash_ctx_.suite && t_crash_ctx_.test)
            std::fprintf(stderr, "  正在执行测试: %s.%s\n",
                         t_crash_ctx_.suite, t_crash_ctx_.test);
        std::fprintf(stderr, "  异常地址: 0x%p\n",
                     (void*)ep->ExceptionRecord->ExceptionAddress);
        switch (ep->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:
                std::fprintf(stderr, "  原因: EXCEPTION_ACCESS_VIOLATION\n");
                if (ep->ExceptionRecord->NumberParameters >= 2) {
                    auto info = ep->ExceptionRecord->ExceptionInformation;
                    const char* op = info[0] == 0 ? "读取" : info[0] == 1 ? "写入" : "执行";
                    std::fprintf(stderr, "  操作: %s 地址 0x%p\n", op, (void*)info[1]);
                }
                break;
            case EXCEPTION_STACK_OVERFLOW:
                std::fprintf(stderr, "  原因: EXCEPTION_STACK_OVERFLOW — 栈溢出\n");
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                std::fprintf(stderr, "  原因: EXCEPTION_INT_DIVIDE_BY_ZERO — 除零\n");
                break;
            default:
                break;
        }
        std::fprintf(stderr, "========================================\n");
        std::fflush(stderr);
        return EXCEPTION_EXECUTE_HANDLER;
    });
}
#endif

// 未捕获异常处理
inline void terminate_handler() {
    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "[CRASH] 未捕获异常 (std::terminate)\n");
    std::fprintf(stderr, "  正在执行测试: %s.%s\n",
                 detail::current_suite().c_str(),
                 detail::current_test().c_str());
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "  异常类型: %s\n", typeid(e).name());
        std::fprintf(stderr, "  异常信息: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "  异常类型: 非 std::exception 类型（无法获取信息）\n");
    }
    std::fprintf(stderr, "========================================\n");
    std::fflush(stderr);
    std::abort();
}

// ==================== 运行器 ====================

inline int run_all_tests(int argc, char** argv) {
    std::setbuf(stdout, NULL);

    // 注册崩溃/异常处理器
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGABRT, crash_handler);
    std::signal(SIGFPE,  crash_handler);
    std::signal(SIGILL,  crash_handler);
    std::set_terminate(terminate_handler);
#ifdef _WIN32
    install_seh_handler();
#endif

    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            detail::verbose() = true;
        }
    }

    std::lock_guard lock(detail::tests_mutex());
    auto& all = detail::tests();

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    std::fprintf(stdout, "[==========] Running %zu tests.\n", all.size());

    for (auto& t : all) {
        std::string full_name = t.suite + "." + t.name;

        // 过滤
        if (!filter.empty()) {
            bool match = false;
            if (filter.find('*') != std::string::npos) {
                auto pos = filter.find('.');
                if (pos != std::string::npos) {
                    auto f_suite = filter.substr(0, pos);
                    auto f_name = filter.substr(pos + 1);
                    if ((f_suite == "*" || f_suite == t.suite) &&
                        (f_name == "*" || f_name == t.name)) {
                        match = true;
                    }
                }
            }
            if (!match && full_name != filter) {
                ++skipped;
                continue;
            }
        }

        detail::current_suite() = t.suite;
        detail::current_test() = t.name;
        detail::current_fail_count() = 0;
#ifdef _WIN32
        t_crash_ctx_ = {t.suite.c_str(), t.name.c_str()};
#endif

        std::fprintf(stdout, "[ RUN      ] %s\n", full_name.c_str());

        try {
            t.fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  FAIL  未捕获异常 [%s]: %s\n",
                         typeid(e).name(), e.what());
            detail::report_failure(t.suite.c_str(), 0,
                std::string("未捕获异常: ") + e.what());
        } catch (...) {
            std::fprintf(stderr, "  FAIL  未捕获非 std::exception 类型异常\n");
            detail::report_failure(t.suite.c_str(), 0,
                "未捕获非 std::exception 类型异常");
        }

        if (detail::current_fail_count() == 0) {
            std::fprintf(stdout, "[       OK ] %s\n", full_name.c_str());
            ++passed;
        } else {
            std::fprintf(stdout, "[  FAILED  ] %s\n", full_name.c_str());
            ++failed;
        }
    }

    std::fprintf(stdout, "[==========] %d tests ran. (%d passed, %d failed, %d skipped)\n",
                 passed + failed, passed, failed, skipped);

    return failed > 0 ? 1 : 0;
}

// ==================== 便捷 main ====================

#define BEN_GEAR_TEST_MAIN()                                                \
    int main(int argc, char** argv) {                                       \
        return ::ben_gear::test::run_all_tests(argc, argv);                 \
    }

}  // namespace ben_gear::test

// ==================== gtest 兼容别名 ====================

// 让现有测试文件无需修改即可编译
namespace testing {
inline int InitGoogleTest(int*, char***) { return 0; }
}

// GTEST 宏兼容 — 映射到 BEN_GEAR 宏
// TEST 宏已定义，无需重复
// EXPECT_*/ASSERT_* 已定义


// ==================== gtest/gmock 兼容层 ====================

namespace testing {

// Test fixture 基类（兼容 ::testing::Test）
class Test {
public:
    virtual ~Test() = default;
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// 参数化测试支持
template <typename T>
class TestWithParam : public Test {
public:
    static const T& GetParam() { return *current_param_; }
    static void set_param(const T* p) { current_param_ = p; }
private:
    static const T* current_param_;
};

template <typename T>
const T* TestWithParam<T>::current_param_ = nullptr;

// HasSubstr 匹配器
inline std::string HasSubstr(const std::string& substr) { return substr; }

// Not 匹配器（简化版）
struct NotMatcher {
    std::string substr;
    NotMatcher(std::string s) : substr(std::move(s)) {}
};
inline NotMatcher Not(const std::string& s) { return NotMatcher(s); }

// Values 生成器
template <typename T>
class ValuesImpl {
public:
    std::vector<T> values;
    ValuesImpl(std::initializer_list<T> vals) : values(vals) {}
};

template <typename T>
ValuesImpl<T> Values(std::initializer_list<T> vals) {
    return ValuesImpl<T>(vals);
}

}  // namespace testing

namespace ben_gear::test::detail {
inline bool matches_that(const std::string& value, const std::string& substr) {
    return value.find(substr) != std::string::npos;
}

inline bool matches_that(const std::string& value, const ::testing::NotMatcher& matcher) {
    return value.find(matcher.substr) == std::string::npos;
}

inline std::string matcher_description(const std::string& substr) {
    return "HasSubstr(\"" + substr + "\")";
}

inline std::string matcher_description(const ::testing::NotMatcher& matcher) {
    return "Not(HasSubstr(\"" + matcher.substr + "\"))";
}
}  // namespace ben_gear::test::detail

// EXPECT_THAT 兼容
#define EXPECT_THAT(value, matcher)                                             \
    do {                                                                        \
        auto value_ = std::string(value);                                       \
        auto matcher_ = (matcher);                                              \
        BEN_GEAR_TEST_ASSERT_IMPL_(__FILE__, __LINE__,                          \
            ::ben_gear::test::detail::matches_that(value_, matcher_),           \
            "EXPECT_THAT(" #value ", " +                                      \
                ::ben_gear::test::detail::matcher_description(matcher_) + ")"); \
    } while (0)

// TEST_F 兼容（fixture 测试）
#define TEST_F(fixture, name)                                                  \
    class ben_gear_fixture_##fixture##_##name : public fixture {              \
    public:                                                                   \
        void RunTest() {                                                      \
            this->SetUp();                                                    \
            this->TestBody();                                                 \
            this->TearDown();                                                 \
        }                                                                     \
        void TestBody();                                                      \
    };                                                                        \
    ::ben_gear::test::TestRegistrar                                           \
        ben_gear_reg_f_##fixture##_##name(                                    \
            #fixture, #name, []() {                                           \
                ben_gear_fixture_##fixture##_##name f_;                       \
                f_.RunTest();                                                 \
            });                                                                \
    void ben_gear_fixture_##fixture##_##name::TestBody()

// TEST_P 兼容（参数化测试 — 简化版，需要手动注册每个参数）
#define TEST_P(fixture, name)                                                  \
    void ben_gear_test_p_##fixture##_##name();                                 \
    static ::ben_gear::test::TestRegistrar                                     \
        ben_gear_reg_p_##fixture##_##name(                                     \
            #fixture, #name, []() {                                            \
                ben_gear_test_p_##fixture##_##name();                           \
            });                                                                 \
    void ben_gear_test_p_##fixture##_##name()

// INSTANTIATE_TEST_SUITE_P — 展开为独立的 TEST 调用
#define INSTANTIATE_TEST_SUITE_P(prefix, fixture, values_impl)                 \
    /* 参数化实例由手动展开处理 */                                               \

// GetParam 兼容
using ::testing::TestWithParam;


// ==================== TmpDirTest 兼容 ====================

// ==================== TmpDirTest 兼容 ====================

#include <filesystem>

namespace bengear::test {

class TmpDirTest : public ::testing::Test {
public:
    std::filesystem::path dir_;

    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path()
             / ("bengear-" + ::ben_gear::test::detail::current_suite()
             + "-" + ::ben_gear::test::detail::current_test());
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    const std::filesystem::path& dir() const { return dir_; }
};

}  // namespace bengear::test

// 补充 ASSERT 宏
#define ASSERT_LT(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) < (b),  "ASSERT_LT(" #a ", " #b ")")
#define ASSERT_LE(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) <= (b), "ASSERT_LE(" #a ", " #b ")")
#define ASSERT_GT(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) > (b),  "ASSERT_GT(" #a ", " #b ")")
#define ASSERT_GE(a, b)  BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, (a) >= (b), "ASSERT_GE(" #a ", " #b ")")
#define ASSERT_STREQ(a, b) BEN_GEAR_TEST_ASSERT_FATAL_(__FILE__, __LINE__, \
    std::strcmp((a), (b)) == 0, "ASSERT_STREQ(" #a ", " #b ")")

#define EXPECT_DOUBLE_EQ(a, b) EXPECT_NEAR(a, b, 1e-10)
