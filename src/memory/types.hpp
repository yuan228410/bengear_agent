#pragma once


namespace ben_gear::memory {


/// 记忆内容类型
enum class MemoryKind { memory, soul, rules, episode };

/// 三层级合并后的记忆内容
struct MergedMemory {
    std::string memory_content;  // MEMORY.md 合并结果
    std::string soul_content;    // SOUL.md 合并结果
    std::string rules_content;   // RULES.md 合并结果
};

/// 层级名
inline const char* tier_name(MemoryKind kind) {
    switch (kind) {
        case MemoryKind::memory: return "MEMORY";
        case MemoryKind::soul: return "SOUL";
        case MemoryKind::rules: return "RULES";
        case MemoryKind::episode: return "episode";
    }
    return "unknown";
}

}  // namespace ben_gear::memory
