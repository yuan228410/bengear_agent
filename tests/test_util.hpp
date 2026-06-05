#pragma once

#include <filesystem>
#include <string>
#include <gtest/gtest.h>

namespace bengear::test {

/// Fixture that creates and cleans up a unique temp directory per test
class TmpDirTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::temp_directory_path()
             / ("bengear-" + std::string(info->test_suite_name())
             + "-" + std::string(info->name()));
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

}  // namespace bengear::test
