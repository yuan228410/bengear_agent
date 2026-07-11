#pragma once

#include "server/api/common.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ben_gear::server::composition {

Json source_context_json(const std::filesystem::path& project_root,
                         std::string_view selected_path,
                         int line,
                         int context_lines,
                         std::int64_t max_file_bytes);
Json source_contexts_from_locations(const std::filesystem::path& project_root,
                                    const Json& locations,
                                    std::string_view kind,
                                    int context_lines,
                                    std::int64_t max_file_bytes,
                                    int max_items);
Json symbol_context_json(const std::filesystem::path& project_root,
                         const Json& snapshot,
                         int context_lines,
                         int max_file_bytes,
                         int max_contexts);
Json dependency_context_json(const std::filesystem::path& project_root,
                             const Json& selected_path_result,
                             int context_lines,
                             int max_file_bytes,
                             int max_contexts);
Json impact_context_json(const Json& snapshot);
Json failure_context_json(const Json& snapshot);
Json readiness_context_json(const Json& snapshot);
Json timeline_context_json(const Json& snapshot);
Json handoff_context_json(const Json& snapshot,
                          const std::string& selected_path,
                          const std::string& query_text,
                          const std::string& symbol);
Json review_context_json(const Json& snapshot);
Json gate_context_json(const Json& snapshot);
Json agent_context_json(const Json& snapshot);
Json action_context_json(const Json& snapshot, const std::string& selected_path);
Json handoff_package_json(const Json& snapshot);
std::string first_command_from_verification(const Json& snapshot);

} // namespace ben_gear::server::composition
