#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::checkpoint {

struct CheckpointFileRecord {
    std::string path;
    bool existed = false;
    std::string content;
    std::string hash;
    std::uintmax_t size = 0;
};

struct CheckpointRecord {
    std::string checkpoint_id;
    std::string session_id;
    std::string description;
    std::string created_at;
    std::vector<CheckpointFileRecord> files;
    bool restored = false;
    std::string restored_at;
};

Json to_json(const CheckpointFileRecord& file);
Json to_json(const CheckpointRecord& record);

} // namespace ben_gear::checkpoint
