#include <gtest/gtest.h>
#include "ben_gear/role/filter.hpp"
#include "ben_gear/role/loader.hpp"
#include "ben_gear/role/types.hpp"

#include <fstream>

TEST(ToolFilter, EmptyWhitelistAllowsAll) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> empty_whitelist;
    ben_gear::role::ToolFilter filter(empty_whitelist);
    EXPECT_TRUE(filter.no_filter());
    EXPECT_TRUE(filter.is_allowed("any_tool"));
}

TEST(ToolFilter, NonEmptyWhitelistFilters) {
    ben_gear::base::container::Vector<ben_gear::base::container::String> whitelist;
    whitelist.push_back(ben_gear::base::container::String("read_file"));
    whitelist.push_back(ben_gear::base::container::String("list_dir"));
    whitelist.push_back(ben_gear::base::container::String("http_get"));
    ben_gear::role::ToolFilter filter(whitelist);
    EXPECT_FALSE(filter.no_filter());
    EXPECT_TRUE(filter.is_allowed("read_file"));
    EXPECT_TRUE(filter.is_allowed("list_dir"));
    EXPECT_TRUE(filter.is_allowed("http_get"));
    EXPECT_FALSE(filter.is_allowed("write_file"));
    EXPECT_FALSE(filter.is_allowed("run_command"));
    EXPECT_EQ(filter.whitelist_size(), 3u);
}

TEST(RoleDefinition, IsToolAllowed) {
    ben_gear::role::RoleDefinition role;
    role.name = ben_gear::base::container::String("lead");
    // empty whitelist = no filter
    EXPECT_TRUE(role.is_tool_allowed("any_tool"));
    EXPECT_TRUE(role.no_filter());
}

TEST(RoleDefinition, RestrictedRole) {
    ben_gear::role::RoleDefinition role;
    role.name = ben_gear::base::container::String("teammate");
    role.tool_whitelist.push_back(ben_gear::base::container::String("read_file"));
    EXPECT_TRUE(role.is_tool_allowed("read_file"));
    EXPECT_FALSE(role.is_tool_allowed("write_file"));
    EXPECT_FALSE(role.no_filter());
}

// --- RoleLoader tests ---

class RoleLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        global_dir_ = std::filesystem::temp_directory_path() / "bengear-rl-global";
        user_dir_ = std::filesystem::temp_directory_path() / "bengear-rl-user";
        workspace_dir_ = std::filesystem::temp_directory_path() / "bengear-rl-ws";
        std::filesystem::create_directories(global_dir_ / "roles");
        std::filesystem::create_directories(user_dir_ / "roles");
        std::filesystem::create_directories(workspace_dir_ / "roles");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(global_dir_, ec);
        std::filesystem::remove_all(user_dir_, ec);
        std::filesystem::remove_all(workspace_dir_, ec);
    }

    void write_role(const std::filesystem::path& base_dir,
                    const std::string& filename,
                    const std::string& json_content) {
        auto path = base_dir / "roles" / filename;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << json_content;
    }

    std::filesystem::path global_dir_;
    std::filesystem::path user_dir_;
    std::filesystem::path workspace_dir_;
};

TEST_F(RoleLoaderTest, EmptyDirsNoRoles) {
    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();
    auto roles = loader.list_roles();
    EXPECT_TRUE(roles.empty());
    EXPECT_FALSE(loader.get_role(ben_gear::base::container::String("any")).has_value());
}

TEST_F(RoleLoaderTest, LoadSingleRole) {
    write_role(global_dir_, "lead.json",
        R"({"name":"lead","description":"Lead agent with full access"})");
    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    auto role = loader.get_role(ben_gear::base::container::String("lead"));
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(std::string(role->name.data(), role->name.size()), "lead");
    EXPECT_EQ(std::string(role->description.data(), role->description.size()),
              "Lead agent with full access");
    EXPECT_EQ(std::string(role->tier.data(), role->tier.size()), "global");
    EXPECT_TRUE(role->no_filter());
}

TEST_F(RoleLoaderTest, LoadRoleWithWhitelist) {
    write_role(global_dir_, "teammate.json",
        R"({"name":"teammate","description":"Restricted teammate","tool_whitelist":["read_file","search"]})");
    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    auto role = loader.get_role(ben_gear::base::container::String("teammate"));
    ASSERT_TRUE(role.has_value());
    EXPECT_FALSE(role->no_filter());
    EXPECT_TRUE(role->is_tool_allowed("read_file"));
    EXPECT_TRUE(role->is_tool_allowed("search"));
    EXPECT_FALSE(role->is_tool_allowed("write_file"));
}

TEST_F(RoleLoaderTest, ThreeTierOverrideLastWins) {
    write_role(global_dir_, "lead.json",
        R"({"name":"lead","description":"Global lead"})");
    write_role(user_dir_, "lead.json",
        R"({"name":"lead","description":"User lead"})");
    write_role(workspace_dir_, "lead.json",
        R"({"name":"lead","description":"Workspace lead"})");

    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    auto role = loader.get_role(ben_gear::base::container::String("lead"));
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(std::string(role->description.data(), role->description.size()),
              "Workspace lead");
    EXPECT_EQ(std::string(role->tier.data(), role->tier.size()), "workspace");
}

TEST_F(RoleLoaderTest, MultipleRolesFromDifferentTiers) {
    write_role(global_dir_, "lead.json",
        R"({"name":"lead","description":"Lead agent"})");
    write_role(user_dir_, "reviewer.json",
        R"({"name":"reviewer","description":"Code reviewer","tool_whitelist":["read_file","search"]})");

    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    auto roles = loader.list_roles();
    EXPECT_EQ(roles.size(), 2u);

    auto lead = loader.get_role(ben_gear::base::container::String("lead"));
    ASSERT_TRUE(lead.has_value());
    EXPECT_EQ(std::string(lead->tier.data(), lead->tier.size()), "global");

    auto reviewer = loader.get_role(ben_gear::base::container::String("reviewer"));
    ASSERT_TRUE(reviewer.has_value());
    EXPECT_EQ(std::string(reviewer->tier.data(), reviewer->tier.size()), "user");
}

TEST_F(RoleLoaderTest, InvalidJsonIgnored) {
    write_role(global_dir_, "bad.json", "not json at all");
    write_role(global_dir_, "good.json",
        R"({"name":"good","description":"Valid role"})");

    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    EXPECT_FALSE(loader.get_role(ben_gear::base::container::String("bad")).has_value());
    EXPECT_TRUE(loader.get_role(ben_gear::base::container::String("good")).has_value());
}

TEST_F(RoleLoaderTest, MissingNameIgnored) {
    write_role(global_dir_, "noname.json",
        R"({"description":"No name field"})");

    ben_gear::role::RoleLoader loader(global_dir_, user_dir_, workspace_dir_);
    loader.discover();

    auto roles = loader.list_roles();
    EXPECT_TRUE(roles.empty());
}
