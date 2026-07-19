#include "test_framework.hpp"

#include "base/core/service_registry.hpp"

using namespace ben_gear::base;

namespace {

struct ITestService {
    virtual ~ITestService() = default;
    virtual int value() const = 0;
};

struct TestServiceImpl : ITestService {
    int value() const override { return 42; }
};

struct AnotherService {
    std::string name = "hello";
};

} // namespace

// ─── ServiceRegistry 测试 ──────────────────────────────────────────

TEST(ServiceRegistryTest, RegisterAndResolve) {
    ServiceRegistry registry;

    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());

    auto* svc = registry.resolve<ITestService>();
    ASSERT_NE(svc, nullptr);
    EXPECT_EQ(svc->value(), 42);
}

TEST(ServiceRegistryTest, ResolveUnregistered) {
    ServiceRegistry registry;
    auto* svc = registry.resolve<ITestService>();
    EXPECT_EQ(svc, nullptr);
}

TEST(ServiceRegistryTest, OverwriteExisting) {
    ServiceRegistry registry;

    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());
    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());

    auto* svc = registry.resolve<ITestService>();
    ASSERT_NE(svc, nullptr);
}

TEST(ServiceRegistryTest, MultipleTypes) {
    ServiceRegistry registry;

    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());
    AnotherService svc;
    registry.register_service<AnotherService>(&svc);

    EXPECT_NE(registry.resolve<ITestService>(), nullptr);
    EXPECT_NE(registry.resolve<AnotherService>(), nullptr);
    EXPECT_EQ(registry.resolve<AnotherService>()->name, "hello");
}

TEST(ServiceRegistryTest, HasAndRemove) {
    ServiceRegistry registry;

    EXPECT_FALSE(registry.has<ITestService>());
    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());
    EXPECT_TRUE(registry.has<ITestService>());
    registry.remove<ITestService>();
    EXPECT_FALSE(registry.has<ITestService>());
}

TEST(ServiceRegistryTest, ClearAll) {
    ServiceRegistry registry;

    registry.register_service<ITestService>(std::make_unique<TestServiceImpl>());
    AnotherService svc;
    registry.register_service<AnotherService>(&svc);

    EXPECT_EQ(registry.size(), 2);

    registry.clear();
    EXPECT_EQ(registry.size(), 0);
    EXPECT_EQ(registry.resolve<ITestService>(), nullptr);
}
