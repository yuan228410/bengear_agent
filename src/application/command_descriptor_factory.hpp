#pragma once

#include "application/command.hpp"
#include "application/request_context.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::application {

class CommandDescriptorFactory {
public:
    CommandDescriptorFactory() = default;
    CommandDescriptorFactory(RequestContext request, container::String project_path);

    CommandDescriptor make(std::string_view action) const;

    CommandDescriptor test_run(std::string_view command_text,
                               std::string_view cwd,
                               int timeout_seconds,
                               int max_output_bytes) const;

    CommandDescriptor patch_apply(const std::vector<std::string>& paths) const;
    CommandDescriptor patch_revert(std::string_view change_id,
                                   const std::vector<std::string>& paths,
                                   bool force) const;

    CommandDescriptor checkpoint_restore(std::string_view checkpoint_id,
                                         const std::vector<std::string>& paths,
                                         bool force) const;
    CommandDescriptor checkpoint_delete(std::string_view checkpoint_id) const;

    CommandDescriptor git_branch(std::string_view action,
                                 std::string_view name,
                                 bool force) const;
    CommandDescriptor git_commit(std::string_view message,
                                 const std::vector<std::string>& paths,
                                 bool all,
                                 bool amend) const;
    CommandDescriptor git_restore(const std::vector<std::string>& paths,
                                  bool staged,
                                  bool worktree) const;
    CommandDescriptor git_worktree(std::string_view action,
                                   std::string_view location,
                                   std::string_view branch,
                                   bool create_branch,
                                   bool force) const;

private:
    void add_paths(CommandDescriptor& command, const std::vector<std::string>& paths) const;

    RequestContext request_;
    container::String project_path_;
};

} // namespace ben_gear::application
