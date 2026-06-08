// 性能测试：对比新旧消息系统的内存占用和性能
// 编译：clang++ -std=c++20 -O2 benchmark_message_performance.cpp -o benchmark

#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>

// 模拟旧的 llm::Message 结构
struct OldMessage {
    std::string role;
    std::string content;
    std::vector<std::string> blocks;
    
    static OldMessage user(const std::string& text) {
        OldMessage msg;
        msg.role = "user";
        msg.content = text;
        return msg;
    }
    
    static OldMessage assistant(const std::string& text) {
        OldMessage msg;
        msg.role = "assistant";
        msg.content = text;
        return msg;
    }
};

// 模拟新的 ACP 消息结构（简化版）
struct NewMessage {
    enum Role { User, Assistant, System, Tool };
    
    Role role;
    std::string text;
    
    static NewMessage user(std::string t) {
        NewMessage msg;
        msg.role = User;
        msg.text = std::move(t);
        return msg;
    }
    
    static NewMessage assistant(std::string t) {
        NewMessage msg;
        msg.role = Assistant;
        msg.text = std::move(t);
        return msg;
    }
};

// 性能测试函数
template<typename MessageT>
void benchmark_creation(int count) {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<MessageT> messages;
    messages.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 0) {
            messages.push_back(MessageT::user("This is a user message number " + std::to_string(i)));
        } else {
            messages.push_back(MessageT::assistant("This is an assistant reply number " + std::to_string(i)));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "创建 " << count << " 条消息耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "平均每条消息: " << (double)duration.count() / count << " μs" << std::endl;
}

template<typename MessageT>
void benchmark_copy(int count) {
    std::vector<MessageT> source;
    source.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 0) {
            source.push_back(MessageT::user("This is a user message number " + std::to_string(i)));
        } else {
            source.push_back(MessageT::assistant("This is an assistant reply number " + std::to_string(i)));
        }
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<MessageT> dest = source;  // 拷贝
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "拷贝 " << count << " 条消息耗时: " << duration.count() << " μs" << std::endl;
}

template<typename MessageT>
void benchmark_move(int count) {
    std::vector<MessageT> source;
    source.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 0) {
            source.push_back(MessageT::user("This is a user message number " + std::to_string(i)));
        } else {
            source.push_back(MessageT::assistant("This is an assistant reply number " + std::to_string(i)));
        }
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<MessageT> dest = std::move(source);  // 移动
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "移动 " << count << " 条消息耗时: " << duration.count() << " μs" << std::endl;
}

int main() {
    const int COUNT = 10000;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  消息系统性能对比测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // 内存占用
    std::cout << "=== 内存占用 ===" << std::endl;
    std::cout << "OldMessage 大小: " << sizeof(OldMessage) << " bytes" << std::endl;
    std::cout << "NewMessage 大小: " << sizeof(NewMessage) << " bytes" << std::endl;
    std::cout << "内存减少: " << (1.0 - (double)sizeof(NewMessage) / sizeof(OldMessage)) * 100 << "%" << std::endl;
    std::cout << std::endl;
    
    // 创建性能
    std::cout << "=== 创建性能 ===" << std::endl;
    std::cout << "旧消息系统:" << std::endl;
    benchmark_creation<OldMessage>(COUNT);
    std::cout << std::endl;
    
    std::cout << "新消息系统:" << std::endl;
    benchmark_creation<NewMessage>(COUNT);
    std::cout << std::endl;
    
    // 拷贝性能
    std::cout << "=== 拷贝性能 ===" << std::endl;
    std::cout << "旧消息系统:" << std::endl;
    benchmark_copy<OldMessage>(COUNT);
    std::cout << std::endl;
    
    std::cout << "新消息系统:" << std::endl;
    benchmark_copy<NewMessage>(COUNT);
    std::cout << std::endl;
    
    // 移动性能
    std::cout << "=== 移动性能 ===" << std::endl;
    std::cout << "旧消息系统:" << std::endl;
    benchmark_move<OldMessage>(COUNT);
    std::cout << std::endl;
    
    std::cout << "新消息系统:" << std::endl;
    benchmark_move<NewMessage>(COUNT);
    std::cout << std::endl;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
