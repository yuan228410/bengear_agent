#pragma once

#include "ben_gear/base/container/string.hpp"

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;

struct ChatRequest {
    container::String system_prompt;
    container::String user_prompt;
};

struct ChatResult {
    int status = 0;
    container::String text;
    container::String raw;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ChatRequest = llm::ChatRequest;
using ChatResult = llm::ChatResult;
}  // namespace ben_gear
