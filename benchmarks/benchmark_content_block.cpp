/**
 * @file benchmark_content_block.cpp
 * @brief ContentBlock 内存占用测试
 */

#include <ben_gear/acp/core/content_block.hpp>
#include <ben_gear/llm/message.hpp>
#include <iostream>
#include <iomanip>
#include <algorithm>

using namespace ben_gear;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ContentBlock Memory Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 测试内存占用
    std::cout << "\n=== 内存占用对比 ===" << std::endl;
    
    std::cout << "llm::ContentBlock:  " << sizeof(llm::ContentBlock) << " bytes" << std::endl;
    std::cout << "acp::ContentBlock:  " << sizeof(acp::ContentBlock) << " bytes" << std::endl;
    std::cout << "acp::TextContent:   " << sizeof(acp::TextContent) << " bytes" << std::endl;
    std::cout << "acp::MediaContent:  " << sizeof(acp::MediaContent) << " bytes" << std::endl;
    std::cout << "acp::ToolUseContent: " << sizeof(acp::ToolUseContent) << " bytes" << std::endl;
    std::cout << "acp::ToolResultContent: " << sizeof(acp::ToolResultContent) << " bytes" << std::endl;
    
    // 计算优化比例
    size_t old_size = sizeof(llm::ContentBlock);
    size_t new_size = sizeof(acp::ContentBlock);
    double reduction = (1.0 - static_cast<double>(new_size) / old_size) * 100.0;
    
    std::cout << "\n=== 优化结果 ===" << std::endl;
    std::cout << "内存减少: " << std::fixed << std::setprecision(1) << reduction << "%" << std::endl;
    std::cout << "从 " << old_size << " bytes 减少到 " << new_size << " bytes" << std::endl;
    
    // 测试 variant 的实际大小
    std::cout << "\n=== std::variant 细节 ===" << std::endl;
    size_t max_member = std::max({
        sizeof(acp::TextContent),
        sizeof(acp::MediaContent),
        sizeof(acp::ToolUseContent),
        sizeof(acp::ToolResultContent)
    });
    std::cout << "variant 最大成员: " << max_member << " bytes" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Benchmark Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
