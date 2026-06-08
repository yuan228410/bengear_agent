#include "ben_gear/base/json/json.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <sstream>

using namespace ben_gear::base::container;

// 简单计时器
struct Timer {
    std::chrono::steady_clock::time_point start;
    Timer() : start(std::chrono::steady_clock::now()) {}
    double elapsed_us() const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(end - start).count();
    }
    double elapsed_ms() const { return elapsed_us() / 1000.0; }
};

// 生成测试 JSON 字符串
std::string make_large_json(size_t entries) {
    std::ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < entries; i++) {
        if (i > 0) oss << ",";
        oss << "\"key" << i << "\":{\"name\":\"value" << i << "\",\"num\":" << i 
            << ",\"active\":" << (i % 2 == 0 ? "true" : "false") << "}";
    }
    oss << "}";
    return oss.str();
}

std::string make_array_json(size_t count) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < count; i++) {
        if (i > 0) oss << ",";
        oss << "{\"id\":" << i << ",\"name\":\"item" << i << "\",\"score\":" << (i * 0.1);
    }
    oss << "]";
    return oss.str();
}

void bench_parse_object() {
    auto json_str = make_large_json(1000);
    std::cout << "=== Parse Object (1000 entries) ===\n";
    
    // Warmup
    auto j = Json::parse(json_str);
    
    const int N = 100;
    Timer t;
    for (int i = 0; i < N; i++) {
        auto j = Json::parse(json_str);
    }
    double ms = t.elapsed_ms() / N;
    std::cout << "  Avg: " << ms << " ms/parse\n";
    std::cout << "  Throughput: " << (json_str.size() / 1024.0 / (ms / 1000.0)) << " KB/s\n";
}

void bench_parse_array() {
    auto json_str = make_array_json(5000);
    std::cout << "=== Parse Array (5000 elements) ===\n";
    
    const int N = 100;
    Timer t;
    for (int i = 0; i < N; i++) {
        auto j = Json::parse(json_str);
    }
    double ms = t.elapsed_ms() / N;
    std::cout << "  Avg: " << ms << " ms/parse\n";
}

void bench_serialize() {
    auto j = Json::parse(make_large_json(500));
    std::cout << "=== Serialize Object (500 entries) ===\n";
    
    const int N = 100;
    Timer t;
    for (int i = 0; i < N; i++) {
        auto s = j.dump();
    }
    double ms = t.elapsed_ms() / N;
    std::cout << "  Compact: " << ms << " ms/dump\n";
    
    Timer t2;
    for (int i = 0; i < N; i++) {
        auto s = j.dump(2);
    }
    ms = t2.elapsed_ms() / N;
    std::cout << "  Pretty: " << ms << " ms/dump\n";
}

void bench_object_access() {
    auto j = Json::parse(make_large_json(1000));
    std::cout << "=== Object Access (1000 entries) ===\n";
    
    const int N = 10000;
    Timer t;
    volatile int64_t sum = 0;
    for (int i = 0; i < N; i++) {
        sum += j["key500"]["num"].as_int();
    }
    double us = t.elapsed_us() / N;
    std::cout << "  j[\"key500\"][\"num\"]: " << us << " us/op\n";
    (void)sum;
}

void bench_array_iteration() {
    auto j = Json::parse(make_array_json(10000));
    std::cout << "=== Array Iteration (10000 elements) ===\n";
    
    const int N = 100;
    Timer t;
    volatile size_t count = 0;
    for (int i = 0; i < N; i++) {
        for (const auto& el : j) {
            count += el.size();
        }
    }
    double ms = t.elapsed_ms() / N;
    std::cout << "  Full iteration: " << ms << " ms\n";
    (void)count;
}

void bench_object_iteration() {
    auto j = Json::parse(make_large_json(1000));
    std::cout << "=== Object Iteration (1000 entries) ===\n";
    
    const int N = 100;
    Timer t;
    volatile size_t count = 0;
    for (int i = 0; i < N; i++) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            count++;
        }
    }
    double ms = t.elapsed_ms() / N;
    std::cout << "  Full iteration: " << ms << " ms\n";
    (void)count;
}

void bench_proxyref_chain() {
    Json j = Json::object();
    std::cout << "=== ProxyRef Chain Write ===\n";
    
    const int N = 10000;
    Timer t;
    for (int i = 0; i < N; i++) {
        j["level1"]["level2"]["value"] = i;
    }
    double us = t.elapsed_us() / N;
    std::cout << "  j[\"a\"][\"b\"][\"c\"] = val: " << us << " us/op\n";
    
    // Read back
    Timer t2;
    volatile int64_t sum = 0;
    for (int i = 0; i < N; i++) {
        sum += j["level1"]["level2"]["value"].as_int();
    }
    us = t2.elapsed_us() / N;
    std::cout << "  j[\"a\"][\"b\"][\"c\"].as_int(): " << us << " us/op\n";
    (void)sum;
}

void bench_parse_realistic() {
    // 模拟 LLM API 响应
    std::string response = R"({
        "id": "chatcmpl-abc123",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "gpt-4",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "Hello! I'm an AI assistant. How can I help you today?"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 25,
            "completion_tokens": 15,
            "total_tokens": 40
        }
    })";
    
    std::cout << "=== Parse Realistic LLM Response ===\n";
    
    const int N = 10000;
    Timer t;
    for (int i = 0; i < N; i++) {
        auto j = Json::parse(response);
    }
    double us = t.elapsed_us() / N;
    std::cout << "  Avg: " << us << " us/parse\n";
}

int main() {
    std::cout << "\n========== JSON Performance Benchmarks ==========\n\n";
    
    bench_parse_object();
    bench_parse_array();
    bench_parse_realistic();
    bench_serialize();
    bench_object_access();
    bench_array_iteration();
    bench_object_iteration();
    bench_proxyref_chain();
    
    std::cout << "\n========== Done ==========\n";
    return 0;
}
