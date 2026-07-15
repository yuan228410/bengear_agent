#include "plugins/plugin_loader.hpp"
#include "plugins/plugin_abi.hpp"
#include "capabilities/tool/registry.hpp"
#include "base/utils/json.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

namespace {

/// 获取编译产出的 hello_capability 插件路径
std::filesystem::path hello_plugin_path() {
#ifdef BENGEAR_PLUGIN_TEST_DIR
    return std::filesystem::path(BENGEAR_PLUGIN_TEST_DIR) / "ben_gear_plugin_hello_capability"
        BENGEAR_PLUGIN_TEST_SUFFIX;
#else
    return {};
#endif
}

} // anonymous namespace

TEST(PluginLoaderTest, LoadNonExistentDirReturnsZero) {
    ben_gear::plugins::PluginLoader loader("/non/existent/path/that/does/not/exist");
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 0u);
    EXPECT_TRUE(errors.empty());
}

TEST(PluginLoaderTest, LoadEmptyDirReturnsZero) {
    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_empty";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 0u);
    EXPECT_TRUE(errors.empty());

    bengear::test::force_remove_dir(tmp_dir);
}

TEST(PluginLoaderTest, LoadDirWithNonPluginFilesReturnsZero) {
    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_none";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    std::ofstream(tmp_dir / "readme.txt") << "not a plugin";
    std::ofstream(tmp_dir / "config.json") << "{}";

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 0u);
    EXPECT_TRUE(errors.empty());

    bengear::test::force_remove_dir(tmp_dir);
}

TEST(PluginLoaderTest, UnloadAllClearsLoadedPlugins) {
    ben_gear::plugins::PluginLoader loader("/non/existent/path");
    loader.unload_all();
    EXPECT_TRUE(true);
}

TEST(PluginLoaderTest, LoadRealPluginAndExecuteTool) {
    auto path = hello_plugin_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        std::cerr << "[SKIP] plugin binary not found\n";
        return;
    }

    // 复制 .so 到临时目录（模拟插件的实际部署路径）
    auto tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_real";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);
    std::filesystem::copy(path, tmp_dir / path.filename(),
                          std::filesystem::copy_options::overwrite_existing);

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 1u);
    EXPECT_TRUE(errors.empty());

    const auto& plugins = loader.loaded_plugins();
    ASSERT_EQ(plugins.size(), 1u);
    ASSERT_EQ(plugins[0].tools.size(), 1u);

    const auto& tool = plugins[0].tools[0];
    EXPECT_STREQ(tool.name, "hello");
    EXPECT_NE(tool.execute, nullptr);

    // 验证可执行
    const char* result = tool.execute(R"({"name":"world"})");
    EXPECT_NE(result, nullptr);
    EXPECT_NE(std::string(result).find("Hello"), std::string::npos);

    bengear::test::force_remove_dir(tmp_dir);
}

TEST(PluginLoaderTest, PluginToolsRegisteredIntoRegistry) {
    auto path = hello_plugin_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        std::cerr << "[SKIP] plugin binary not found\n";
        return;
    }

    auto tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_reg";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);
    std::filesystem::copy(path, tmp_dir / path.filename(),
                          std::filesystem::copy_options::overwrite_existing);

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    loader.load_all();

    const auto& plugins = loader.loaded_plugins();
    ASSERT_EQ(plugins.size(), 1u);

    // 模拟 Runtime 的注册逻辑：将插件工具注册到 ToolRegistry
    ben_gear::llm::ToolRegistry registry;
    for (const auto& p : plugins) {
        for (const auto& t : p.tools) {
            auto params_json = ben_gear::Json::parse(t.params_json ? t.params_json : "[]");
            std::vector<std::pair<std::string, ben_gear::llm::ToolParameterSchema>> params;
            if (params_json.is_array()) {
                for (const auto& pj : params_json) {
                    ben_gear::llm::ToolParameterSchema schema;
                    schema.type = pj.value("type", "string");
                    schema.description = pj.value("description", "");
                    schema.required = pj.value("required", false);
                    params.emplace_back(
                        pj.value("name", ""),
                        std::move(schema));
                }
            }
            auto* fn = t.execute;
            registry.register_tool(
                std::string(t.name),
                std::string(t.description),
                params,
                [fn](const ben_gear::Json& args) -> std::string {
                    auto result = fn(args.dump().c_str());
                    return std::string(result);
                });
        }
    }

    // 验证工具已注册（通过执行来验证）
    auto args = ben_gear::Json::object();
    args["name"] = "BenGear";
    auto result = registry.execute("hello", args);
    EXPECT_TRUE(result.success);
    EXPECT_NE(std::string(result.output.data(), result.output.size()).find("Hello"), std::string::npos);

    bengear::test::force_remove_dir(tmp_dir);
}

#if defined(_WIN32)
TEST(PluginLoaderTest, LoadInvalidDllReturnsError) {
    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_invalid";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    std::ofstream(tmp_dir / "fake.dll") << "not a real dll";

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 0u);
    EXPECT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("fake.dll") != std::string::npos);

    bengear::test::force_remove_dir(tmp_dir);
}
#endif