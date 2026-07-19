#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/utils/string_utils.hpp"

namespace ben_gear::config {

enum class Provider { openai, anthropic };

/// LLM Provider 配置
struct LlmSettings {
    Provider provider = Provider::openai;
    std::string api_key;
    std::string base_url = std::string("https://api.openai.com");
    std::string api_url;
    std::string model = std::string("gpt-4o-mini");
    int max_tokens = 1024;
    double temperature = 0.2;
    bool stream = true;
    std::int64_t context_length = 256000;
    std::unordered_map<std::string, std::string> headers;
    std::string anthropic_api_version;
    bool reasoning = false;
    std::string display_name;
    std::string config_provider_name;
    std::vector<std::string> fallback_models;
};

/// LLM 请求重试配置
struct LlmRequestRetrySettings {
    int max_attempts = 5;
    unsigned int initial_delay_ms = 200;
    unsigned int max_delay_ms = 3000;
};

// ─── 工具函数 ──────────────────────────────────────────────────────

inline std::string provider_name(Provider provider) {
    return provider == Provider::anthropic ? std::string("anthropic") : std::string("openai");
}

inline Provider parse_provider(std::string_view value) {
    const auto normalized = base::utils::to_lower(base::utils::trim(value));
    return normalized == "anthropic" || normalized == "claude" ? Provider::anthropic : Provider::openai;
}

} // namespace ben_gear::config
