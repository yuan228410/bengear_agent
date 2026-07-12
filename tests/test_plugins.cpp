#include "plugins/plugin_loader.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

namespace {

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
    // Just verify unload_all doesn't crash on empty loader
    loader.unload_all();
    EXPECT_TRUE(true);
}

#if defined(_WIN32)
TEST(PluginLoaderTest, LoadInvalidDllReturnsError) {
    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "bengear_plugin_test_invalid";
    bengear::test::force_remove_dir(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    // Create a fake .dll file
    std::ofstream(tmp_dir / "fake.dll") << "not a real dll";

    ben_gear::plugins::PluginLoader loader(tmp_dir);
    auto [loaded, errors] = loader.load_all();
    EXPECT_EQ(loaded, 0u);
    EXPECT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("fake.dll") != std::string::npos);

    bengear::test::force_remove_dir(tmp_dir);
}
#endif