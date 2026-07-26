#include "base/core/runtime_boundary.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

std::vector<std::filesystem::path> list_files(const std::filesystem::path& root, const std::string& suffix) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().string().ends_with(suffix)) files.push_back(entry.path());
    }
    return files;
}

} // namespace

TEST(CoreRuntimeBoundaryTest, SerializesStableRuntimeBoundaryModel) {
    ben_gear::base::core::RuntimeBoundary boundary;
    boundary.operation.operation_id = "op-1";
    boundary.operation.capability = ben_gear::base::core::RuntimeCapability::patch_apply;
    boundary.operation.scope = ben_gear::base::core::MutationScope::workspace_write;
    boundary.operation.actor = "alice";
    boundary.operation.description = "apply change";
    boundary.operation.workspace.username = "alice";
    boundary.operation.workspace.workspace_name = "default";
    boundary.operation.workspace.project_path = "/repo";
    boundary.operation.workspace.session_id = "sid-1";
    boundary.permission_gates.push_back(ben_gear::base::core::PermissionGateRef{
        "perm-1", "workspace.write", ben_gear::base::core::MutationScope::workspace_write, ben_gear::Json{{"path", "hello.txt"}}});
    boundary.patches.push_back(ben_gear::base::core::PatchRef{"change-1", "update hello", 1, 2, 1});
    boundary.git_refs.push_back(ben_gear::base::core::GitRef{"/repo", "main", "abc123", false});
    boundary.checkpoints.push_back(ben_gear::base::core::CheckpointRef{"checkpoint-1", "before patch", 1});
    auto json = ben_gear::base::core::to_json(boundary);

    EXPECT_EQ(json["operation"]["capability"].get<std::string>(), "patch_apply");
    EXPECT_EQ(json["operation"]["scope"].get<std::string>(), "workspace_write");
    EXPECT_EQ(json["operation"]["workspace"]["session_id"].get<std::string>(), "sid-1");
    EXPECT_EQ(json["permission_gates"].size(), static_cast<size_t>(1));
    EXPECT_EQ(json["patches"][0]["files_changed"].get<int>(), 1);
    EXPECT_EQ(json["git_refs"][0]["clean"].get<bool>(), false);
    EXPECT_EQ(json["checkpoints"][0]["checkpoint_id"].get<std::string>(), "checkpoint-1");
}

TEST(CoreRuntimeBoundaryTest, CoreHeadersDoNotDependOnRuntimeOrAdapters) {
    std::filesystem::path root = std::filesystem::current_path();
    // 向上查找直到找到 src/ 目录
    std::filesystem::path core_root;
    for (int i = 0; i < 10; ++i) {
        core_root = root / "src" / "base" / "core";
        if (std::filesystem::exists(core_root)) break;
        root = root.parent_path();
    }
    ASSERT_TRUE(std::filesystem::exists(core_root));

    const auto headers = list_files(core_root, ".hpp");
    ASSERT_FALSE(headers.empty());

    const std::vector<std::string> forbidden = {
        "application/", "agent/", "cli/", "server/",
        "workspace/", "capabilities/patch/", "capabilities/git/",
        "capabilities/checkpoint/", "capabilities/permission/",
    };

    for (const auto& header : headers) {
        const auto content = read_file(header);
        for (const auto& include : forbidden) {
            EXPECT_EQ(content.find(include), std::string::npos);
        }
    }
}

TEST(CoreRuntimeBoundaryTest, ApplicationRequestContextIsCoreRequestContext) {
    ben_gear::base::core::RequestContext request;
    request.username = "alice";
    request.workspace_name = "default";
    request.session_id = "sid-1";

    const auto json = ben_gear::base::core::to_json(request);

    EXPECT_EQ(json["username"].get<std::string>(), "alice");
    EXPECT_EQ(json["workspace_name"].get<std::string>(), "default");
    EXPECT_EQ(json["session_id"].get<std::string>(), "sid-1");
}
