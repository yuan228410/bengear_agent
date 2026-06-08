/**
 * @file benchmark_message.cpp
 * @brief llm::Message vs acp::ACPMessage 性能对比测试
 */

#include <ben_gear/llm/message.hpp>
#include <ben_gear/acp/acp.hpp>
#include <ben_gear/base/log/logger.hpp>

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace ben_gear;

// ==================== 计时工具 ====================

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ms() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }
    
private:
    std::chrono::high_resolution_clock::time_point start_;
};

// ==================== 测试函数 ====================

// 测试 1：简单文本消息创建
void benchmark_simple_text_creation() {
    constexpr int N = 100000;
    
    std::cout << "\n=== 测试 1：简单文本消息创建 ===" << std::endl;
    
    // llm::Message
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            auto msg = llm::Message::user("Hello, this is a test message");
            (void)msg;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "llm::Message:  " << std::setw(8) << elapsed << " ms" 
                  << " (" << N << " 次)" << std::endl;
    }
    
    // acp::ACPMessage
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            auto msg = acp::ACPMessage::user_message("Hello, this is a test message");
            (void)msg;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "acp::ACPMessage: " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次)" << std::endl;
    }
}

// 测试 2：带工具调用的消息创建
void benchmark_tool_call_creation() {
    constexpr int N = 50000;
    
    std::cout << "\n=== 测试 2：带工具调用的消息创建 ===" << std::endl;
    
    // llm::Message
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            llm::Message msg;
            msg.role = llm::MessageRole::assistant;
            msg.content = "Let me check the weather";
            
            llm::ToolCallRequest call;
            call.id = "call_123";
            call.name = "get_weather";
            call.arguments = Json{{"city", "Beijing"}};
            msg.blocks.push_back(llm::ContentBlock::tool_use_block(call));
            
            (void)msg;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "llm::Message:  " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次)" << std::endl;
    }
    
    // acp::ACPMessage
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            acp::ACPMessage msg;
            msg.set_role(acp::Role::Assistant);
            msg.add_text("Let me check the weather");
            
            llm::ToolCallRequest call;
            call.id = "call_123";
            call.name = "get_weather";
            call.arguments = Json{{"city", "Beijing"}};
            msg.add_tool_use(call);
            
            (void)msg;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "acp::ACPMessage: " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次)" << std::endl;
    }
}

// 测试 3：序列化性能
void benchmark_serialization() {
    constexpr int N = 10000;
    
    std::cout << "\n=== 测试 3：序列化性能 ===" << std::endl;
    
    // llm::Message
    {
        llm::Message msg = llm::Message::user("Test message for serialization");
        msg.blocks.push_back(llm::ContentBlock::text_block("Additional content"));
        
        Timer timer;
        for (int i = 0; i < N; ++i) {
            Json j = msg.to_openai_format();
            (void)j;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "llm::Message (OpenAI):  " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次)" << std::endl;
    }
    
    // acp::ACPMessage
    {
        acp::ACPMessage msg = acp::ACPMessage::user_message("Test message for serialization");
        msg.add_text("Additional content");
        
        Timer timer;
        for (int i = 0; i < N; ++i) {
            Json j = msg.to_json();
            (void)j;
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "acp::ACPMessage (JSON): " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次)" << std::endl;
    }
}

// 测试 4：内存占用
void benchmark_memory_usage() {
    std::cout << "\n=== 测试 4：内存占用 ===" << std::endl;
    
    // llm::Message
    {
        llm::Message msg = llm::Message::user("Test");
        std::cout << "llm::Message:  " << sizeof(msg) << " bytes (结构体大小)" << std::endl;
        std::cout << "  ContentBlock: " << sizeof(llm::ContentBlock) << " bytes" << std::endl;
    }
    
    // acp::ACPMessage
    {
        acp::ACPMessage msg = acp::ACPMessage::user_message("Test");
        std::cout << "acp::ACPMessage: " << sizeof(msg) << " bytes (结构体大小)" << std::endl;
        std::cout << "  ContentBlock: " << sizeof(acp::ContentBlock) << " bytes" << std::endl;
    }
}

// 测试 5：ConversationHistory vs 多个 ACPMessage
void benchmark_history() {
    constexpr int N = 1000;
    constexpr int MSG_COUNT = 100;
    
    std::cout << "\n=== 测试 5：对话历史管理 ===" << std::endl;
    
    // llm::ConversationHistory
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            llm::ConversationHistory history;
            for (int j = 0; j < MSG_COUNT; ++j) {
                history.add_user("User message");
                history.add_assistant("Assistant response");
            }
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "llm::ConversationHistory: " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次 × " << MSG_COUNT << " 条消息)" << std::endl;
    }
    
    // 多个 ACPMessage
    {
        Timer timer;
        for (int i = 0; i < N; ++i) {
            container::Vector<acp::ACPMessage> messages;
            for (int j = 0; j < MSG_COUNT; ++j) {
                messages.push_back(acp::ACPMessage::user_message("User message"));
                messages.push_back(acp::ACPMessage::assistant_message("Assistant response"));
            }
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "Vector<ACPMessage>:      " << std::setw(8) << elapsed << " ms"
                  << " (" << N << " 次 × " << MSG_COUNT << " 条消息)" << std::endl;
    }
}

// ==================== 主函数 ====================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Message Performance Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    
    benchmark_simple_text_creation();
    benchmark_tool_call_creation();
    benchmark_serialization();
    benchmark_memory_usage();
    benchmark_history();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Benchmark Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
