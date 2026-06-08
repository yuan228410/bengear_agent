/**
 * @file test_memory_layout.cpp
 * @brief 测试 ContentBlock 内存布局（无需链接依赖）
 */

#include <iostream>
#include <iomanip>
#include <variant>
#include <optional>
#include <string>

// 模拟 container::String（SSO 优化）
class MockString {
    char buffer[24];  // SSO buffer
public:
    MockString() = default;
    MockString(const char*) {}
};

// 模拟 Json
class MockJson {};

// 模拟 Source
struct MockSource {
    int type = 0;
    MockString media_type;
    MockString data;
};

// 模拟 ToolCallRequest
struct MockToolCallRequest {
    MockString id;
    MockString name;
    MockJson arguments;
};

// 模拟 ToolCallResult
struct MockToolCallResult {
    MockString tool_call_id;
    MockString output;
};

// ==================== llm::ContentBlock 风格 ====================
struct LLMContentBlock {
    MockString type;
    std::optional<MockString> text;
    std::optional<MockJson> data;
};

// ==================== acp::ContentBlock 优化后 ====================
struct TextContent {
    MockString text;
    bool is_thinking = false;
};

struct MediaContent {
    int type;
    MockSource source;
};

struct ToolUseContent {
    MockToolCallRequest call;
};

struct ToolResultContent {
    MockToolCallResult result;
};

using ACPContentBlock = std::variant<
    TextContent,
    MediaContent,
    ToolUseContent,
    ToolResultContent
>;

// ==================== acp::ContentBlock 优化前 ====================
struct OldContentBlock {
    int type;
    MockString text;
    MockSource source;
    MockToolCallRequest tool_use;
    MockToolCallResult tool_result;
};

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ContentBlock Memory Layout Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n=== 内存占用对比 ===" << std::endl;
    
    std::cout << "llm::ContentBlock 风格: " << sizeof(LLMContentBlock) << " bytes" << std::endl;
    std::cout << "acp::ContentBlock 优化前: " << sizeof(OldContentBlock) << " bytes" << std::endl;
    std::cout << "acp::ContentBlock 优化后: " << sizeof(ACPContentBlock) << " bytes" << std::endl;
    
    std::cout << "\n=== 各类型大小 ===" << std::endl;
    std::cout << "TextContent: " << sizeof(TextContent) << " bytes" << std::endl;
    std::cout << "MediaContent: " << sizeof(MediaContent) << " bytes" << std::endl;
    std::cout << "ToolUseContent: " << sizeof(ToolUseContent) << " bytes" << std::endl;
    std::cout << "ToolResultContent: " << sizeof(ToolResultContent) << " bytes" << std::endl;
    
    std::cout << "\n=== 优化效果 ===" << std::endl;
    size_t old_size = sizeof(OldContentBlock);
    size_t new_size = sizeof(ACPContentBlock);
    double reduction = (1.0 - static_cast<double>(new_size) / old_size) * 100.0;
    
    std::cout << "内存减少: " << std::fixed << std::setprecision(1) << reduction << "%" << std::endl;
    std::cout << "从 " << old_size << " bytes 减少到 " << new_size << " bytes" << std::endl;
    
    // 计算节省的内存（假设 1000 个 ContentBlock）
    std::cout << "\n=== 实际场景对比（1000 个 ContentBlock）===" << std::endl;
    std::cout << "优化前: " << (old_size * 1000 / 1024.0) << " KB" << std::endl;
    std::cout << "优化后: " << (new_size * 1000 / 1024.0) << " KB" << std::endl;
    std::cout << "节省: " << ((old_size - new_size) * 1000 / 1024.0) << " KB" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Test Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
