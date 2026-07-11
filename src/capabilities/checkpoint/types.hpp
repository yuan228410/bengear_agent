#pragma once

#include "base/utils/json.hpp"

#include <cstdint>
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

struct CheckpointCreateResult {
    std::string checkpoint_id;
    CheckpointRecord checkpoint;
};

struct CheckpointListEntry {
    std::string checkpoint_id;
    std::string description;
    std::string created_at;
    bool restored = false;
    int files = 0;
};

struct CheckpointListResult {
    std::vector<CheckpointListEntry> checkpoints;
};

struct CheckpointReadResult {
    CheckpointRecord checkpoint;
};

struct CheckpointRestoreResult {
    std::string checkpoint_id;
    std::vector<std::string> restored;
};

struct CheckpointRemoveResult {
    std::string checkpoint_id;
};

Json to_json(const CheckpointFileRecord& file);
Json to_json(const CheckpointRecord& record);
Json to_json(const CheckpointCreateResult& result);
Json to_json(const CheckpointListEntry& entry);
Json to_json(const CheckpointListResult& result);
Json to_json(const CheckpointReadResult& result);
Json to_json(const CheckpointRestoreResult& result);
Json to_json(const CheckpointRemoveResult& result);

} // namespace ben_gear::checkpoint
