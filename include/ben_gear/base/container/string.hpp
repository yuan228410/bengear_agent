#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <functional>

namespace ben_gear::base::container {

/// 字符串配置
struct StringConfig {
    size_t sso_size = 23;    ///< 小字符串优化大小（<= 23 字节）
    bool use_pool = false;   ///< 是否使用内存池
};

/// 高性能字符串
/// 特性：
/// - 小字符串优化（SSO）：<= 23 字节无堆分配
/// - 移动语义：避免不必要的拷贝
/// - 零拷贝子串：StringView 支持
class String {
public:
    static constexpr size_t npos = std::string_view::npos;
    
    // ==================== 构造/析构 ====================
    
    /// 默认构造
    String() noexcept : small_{}, is_small_(true) {
        small_.data[0] = '\0';
        small_.size = 0;
    }
    
    /// 从 C 字符串构造
    String(const char* str) : String() {
        if (str) {
            assign(str, std::strlen(str));
        }
    }
    
    /// 从 C 字符串构造（指定长度）
    String(const char* str, size_t len) : String() {
        if (str && len > 0) {
            assign(str, len);
        }
    }
    
    /// 从 std::string 构造
    String(const std::string& str) : String(str.c_str(), str.size()) {}
    
    /// 从 std::string_view 构造
    String(std::string_view view) : String(view.data(), view.size()) {}
    
    /// 拷贝构造
    String(const String& other) : String() {
        assign(other.data(), other.size());
    }
    
    /// 移动构造
    String(String&& other) noexcept : String() {
        swap(other);
    }
    
    /// 析构
    ~String() {
        if (!is_small_) {
            ::operator delete(large_.ptr);
        }
    }
    
    // ==================== 赋值 ====================
    
    String& operator=(const String& other) {
        if (this != &other) {
            assign(other.data(), other.size());
        }
        return *this;
    }
    
    String& operator=(String&& other) noexcept {
        if (this != &other) {
            swap(other);
            other.clear();
        }
        return *this;
    }
    
    String& operator=(const char* str) {
        if (str) {
            assign(str, std::strlen(str));
        } else {
            clear();
        }
        return *this;
    }
    
    String& operator=(std::string_view view) {
        assign(view.data(), view.size());
        return *this;
    }
    
    String& operator=(const std::string& str) {
        assign(str.c_str(), str.size());
        return *this;
    }
    
    // ==================== 访问 ====================
    
    /// 获取数据指针
    const char* data() const noexcept {
        return is_small_ ? small_.data : large_.ptr;
    }
    
    /// 获取 C 字符串
    const char* c_str() const noexcept {
        return data();
    }
    
    /// 转换为 std::string
    std::string to_std_string() const {
        return std::string(data(), size());
    }
    
    /// 隐式转换为 std::string
    operator std::string() const {
        return to_std_string();
    }
    
    /// 获取大小
    size_t size() const noexcept {
        return is_small_ ? small_.size : large_.size;
    }
    
    /// 获取长度（同 size）
    size_t length() const noexcept {
        return size();
    }
    
    /// 是否为空
    bool empty() const noexcept {
        return size() == 0;
    }
    
    /// 获取容量
    size_t capacity() const noexcept {
        return is_small_ ? sso_capacity : large_.capacity;
    }
    
    /// 访问字符
    char operator[](size_t pos) const noexcept {
        return data()[pos];
    }
    
    /// 访问字符（带边界检查）
    char at(size_t pos) const {
        if (pos >= size()) {
            throw std::out_of_range("String index out of range");
        }
        return data()[pos];
    }
    
    /// 获取首字符
    char front() const noexcept {
        return data()[0];
    }
    
    /// 获取尾字符
    char back() const noexcept {
        return data()[size() - 1];
    }
    
    // ==================== 操作 ====================
    
    /// 清空
    void clear() noexcept {
        if (!is_small_) {
            ::operator delete(large_.ptr);
        }
        small_.data[0] = '\0';
        small_.size = 0;
        is_small_ = true;
    }
    
    /// 预留容量
    void reserve(size_t capacity);
    
    /// 调整大小
    void resize(size_t size, char fill = '\0');
    
    /// 追加字符串
    String& append(const String& other) {
        return append(other.data(), other.size());
    }
    
    String& append(const char* str, size_t len) {
        if (str && len > 0) {
            const size_t old_size = size();
            const size_t new_size = old_size + len;
            
            // 确保容量足够
            if (new_size > capacity()) {
                reserve(new_size * 2);
            }
            
            // 追加数据
            char* dest = const_cast<char*>(data()) + old_size;
            std::memcpy(dest, str, len);
            dest[len] = '\0';
            
            // 更新大小
            if (is_small_) {
                small_.size = static_cast<uint8_t>(new_size);
            } else {
                large_.size = new_size;
            }
        }
        return *this;
    }
    
    String& append(const char* str) {
        if (str) {
            append(str, std::strlen(str));
        }
        return *this;
    }
    
    String& append(std::string_view view) {
        return append(view.data(), view.size());
    }
    
    String& append(char c) {
        return append(&c, 1);
    }
    
    String& operator+=(const String& other) {
        return append(other);
    }
    
    String& operator+=(const char* str) {
        return append(str);
    }
    
    String& operator+=(char c) {
        return append(c);
    }

    String& operator+=(std::string_view view) {
        return append(view);
    }
    
    /// 子串（返回新字符串）
    String substr(size_t pos = 0, size_t len = npos) const {
        if (pos > size()) {
            throw std::out_of_range("String substr out of range");
        }
        
        const size_t max_len = size() - pos;
        const size_t actual_len = (std::min)(len, max_len);
        
        return String(data() + pos, actual_len);
    }
    
    /// 交换
    void swap(String& other) noexcept {
        std::swap(small_, other.small_);
        std::swap(is_small_, other.is_small_);
    }
    
    // ==================== 比较 ====================
    
    int compare(const String& other) const noexcept {
        const size_t min_size = (std::min)(size(), other.size());
        int result = std::memcmp(data(), other.data(), min_size);
        
        if (result == 0) {
            if (size() < other.size()) return -1;
            if (size() > other.size()) return 1;
        }
        
        return result;
    }
    
    /// 比较子串
    int compare(size_t pos, size_t len, const char* str) const noexcept {
        if (pos > size()) return 1;
        const size_t max_len = size() - pos;
        const size_t actual_len = (std::min)(len, max_len);
        const size_t str_len = std::strlen(str);
        int result = std::memcmp(data() + pos, str, (std::min)(actual_len, str_len));
        
        if (result == 0) {
            if (actual_len < str_len) return -1;
            if (actual_len > str_len) return 1;
        }
        
        return result;
    }
    
    int compare(size_t pos, size_t len, const String& other) const noexcept {
        return compare(pos, len, other.c_str());
    }
    
    bool operator==(const String& other) const noexcept {
        return compare(other) == 0;
    }
    
    bool operator!=(const String& other) const noexcept {
        return compare(other) != 0;
    }
    
    bool operator<(const String& other) const noexcept {
        return compare(other) < 0;
    }
    
    bool operator<=(const String& other) const noexcept {
        return compare(other) <= 0;
    }
    
    bool operator>(const String& other) const noexcept {
        return compare(other) > 0;
    }
    
    bool operator>=(const String& other) const noexcept {
        return compare(other) >= 0;
    }
    
    // ==================== 字符串拼接 ====================
    
    String operator+(const String& other) const {
        String result;
        result.reserve(size() + other.size());
        result.append(data(), size());
        result.append(other.data(), other.size());
        return result;
    }
    
    String operator+(const char* str) const {
        String result;
        const size_t str_len = std::strlen(str);
        result.reserve(size() + str_len);
        result.append(data(), size());
        result.append(str, str_len);
        return result;
    }
    
    friend String operator+(const char* lhs, const String& rhs) {
        String result;
        const size_t lhs_len = std::strlen(lhs);
        result.reserve(lhs_len + rhs.size());
        result.append(lhs, lhs_len);
        result.append(rhs.data(), rhs.size());
        return result;
    }
    
    // ==================== 转换 ====================
    
    /// 转换为 std::string_view
    operator std::string_view() const noexcept {
        return std::string_view(data(), size());
    }
    
    // ==================== 查找 ====================
    
    size_t find(char c, size_t pos = 0) const noexcept {
        const char* ptr = static_cast<const char*>(
            memchr(data() + pos, c, size() - pos)
        );
        return ptr ? ptr - data() : npos;
    }
    
    size_t find(const char* str, size_t pos = 0) const noexcept {
        return find(str, pos, std::strlen(str));
    }
    
    size_t find(const char* str, size_t pos, size_t len) const noexcept {
        if (len == 0) return pos;
        if (pos + len > size()) return npos;
        
        const char* ptr = static_cast<const char*>(
            memmem(data() + pos, size() - pos, str, len)
        );
        
        return ptr ? ptr - data() : npos;
    }
    
    size_t find(std::string_view view, size_t pos = 0) const noexcept {
        return find(view.data(), pos, view.size());
    }
    
private:
    static constexpr size_t sso_capacity = 22;  // 优化为 22 字节（更常见的 SSO 大小）
    
    void assign(const char* str, size_t len) {
        if (len <= sso_capacity) {
            // 使用 SSO - 优化：先释放大字符串内存
            if (!is_small_) {
                ::operator delete(large_.ptr);
                is_small_ = true;
            }
            // 优化：使用 memcpy 而不是循环
            if (len > 0) {
                std::memcpy(small_.data, str, len);
            }
            small_.data[len] = '\0';
            small_.size = static_cast<uint8_t>(len);
        } else {
            // 使用堆
            if (is_small_) {
                // 从 SSO 转换到堆
                is_small_ = false;
                large_.ptr = static_cast<char*>(::operator new(len + 1));
                large_.capacity = len;
            } else if (len > large_.capacity) {
                // 需要重新分配
                ::operator delete(large_.ptr);
                large_.ptr = static_cast<char*>(::operator new(len + 1));
                large_.capacity = len;
            }
            // 优化：直接拷贝到目标
            std::memcpy(large_.ptr, str, len);
            large_.ptr[len] = '\0';
            large_.size = len;
        }
    }
    
    // 优化：使用 union 减少内存占用
    struct SmallData {
        char data[sso_capacity + 1];  // 23 字节
        uint8_t size;                  // 1 字节
        // 总共 24 字节，完美对齐
    };
    
    struct LargeData {
        char* ptr;
        size_t size;
        size_t capacity;
    };
    
    // 优化：使用 union 确保内存布局最优
    union {
        SmallData small_;
        LargeData large_;
    };
    
    // 优化：使用 bool 而不是 enum，减少内存占用
    bool is_small_;
    
    // 优化：添加编译器优化提示
    __attribute__((always_inline))
    bool is_small_inline() const noexcept {
        return is_small_;
    }
};

}  // namespace ben_gear::base::container

// 为 container::String 提供 std::ostream 支持
#include <ostream>

inline std::ostream& operator<<(std::ostream& os, const ben_gear::base::container::String& str) {
    return os << str.c_str();
}

// 为 container::String 提供 std::hash 特化
namespace std {
template<>
struct hash<ben_gear::base::container::String> {
    size_t operator()(const ben_gear::base::container::String& s) const noexcept {
        size_t result = 0;
        const char* data = s.data();
        for (size_t i = 0; i < s.size(); ++i) {
            result = result * 31 + static_cast<size_t>(data[i]);
        }
        return result;
    }
};
}  // namespace std

// 为 container::String 提供 nlohmann/json 序列化（零分配，走 string_view）
#include <nlohmann/json.hpp>
namespace nlohmann {
template <>
struct adl_serializer<ben_gear::base::container::String> {
    static void to_json(json& j, const ben_gear::base::container::String& str) {
        j = std::string_view(str.data(), str.size());
    }
    static void from_json(const json& j, ben_gear::base::container::String& str) {
        str = ben_gear::base::container::String(j.get_ref<const std::string&>());
    }
};
}  // namespace nlohmann
