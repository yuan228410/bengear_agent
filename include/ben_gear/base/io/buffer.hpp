#pragma once

#include "ben_gear/base/memory/pool.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ben_gear::base::io {

/// 高性能缓冲区
/// 支持自动扩容、内存池
class Buffer {
public:
    /// 构造函数
    explicit Buffer(size_t initial_capacity = 1024, memory::MemoryPool* pool = nullptr)
        : pool_(pool)
        , capacity_(initial_capacity)
        , size_(0) {
        if (pool_) {
            data_ = static_cast<char*>(pool_->allocate(initial_capacity));
        } else {
            data_ = static_cast<char*>(::operator new(initial_capacity));
        }
    }
    
    /// 析构函数
    ~Buffer() {
        if (data_) {
            if (pool_) {
                pool_->deallocate(data_, capacity_);
            } else {
                ::operator delete(data_);
            }
        }
    }
    
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    /// 移动构造
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , capacity_(other.capacity_)
        , size_(other.size_)
        , pool_(other.pool_) {
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }
    
    /// 移动赋值
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            if (data_) {
                if (pool_) {
                    pool_->deallocate(data_, capacity_);
                } else {
                    ::operator delete(data_);
                }
            }
            data_ = other.data_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
        }
        return *this;
    }
    
    /// 获取数据
    char* data() noexcept { return data_; }
    const char* data() const noexcept { return data_; }
    
    /// 获取大小
    size_t size() const noexcept { return size_; }
    
    /// 获取容量
    size_t capacity() const noexcept { return capacity_; }
    
    /// 是否为空
    bool empty() const noexcept { return size_ == 0; }
    
    /// 预留容量
    void reserve(size_t capacity) {
        if (capacity > capacity_) {
            char* new_data;
            if (pool_) {
                new_data = static_cast<char*>(pool_->allocate(capacity));
            } else {
                new_data = static_cast<char*>(::operator new(capacity));
            }
            if (data_ && size_ > 0) {
                std::memcpy(new_data, data_, size_);
            }
            // 保证 null terminator：若 buffer 被当 C 字符串使用，reserve 后 size_ 位置补 '\0'
            new_data[size_] = '\0';
            if (data_) {
                if (pool_) {
                    pool_->deallocate(data_, capacity_);
                } else {
                    ::operator delete(data_);
                }
            }
            data_ = new_data;
            capacity_ = capacity;
        }
    }
    
    /// 调整大小
    void resize(size_t size) {
        if (size > capacity_) {
            reserve(size * 2);
        }
        size_ = size;
    }
    
    /// 清空
    void clear() noexcept {
        size_ = 0;
    }
    
    /// 追加数据
    void append(const void* data, size_t len) {
        if (size_ + len > capacity_) {
            reserve(std::max(size_ + len, capacity_ * 2));
        }
        std::memcpy(data_ + size_, data, len);
        size_ += len;
    }
    
    void append(const char* str) {
        if (str) {
            append(str, std::strlen(str));
        }
    }
    
    void append(char c) {
        if (size_ + 1 > capacity_) {
            reserve(capacity_ * 2);
        }
        data_[size_++] = c;
    }
    
    /// 读取数据
    size_t read(void* dest, size_t offset, size_t len) const {
        if (offset >= size_) {
            return 0;
        }
        const size_t available = size_ - offset;
        const size_t actual = std::min(len, available);
        std::memcpy(dest, data_ + offset, actual);
        return actual;
    }
    
    /// 交换
    void swap(Buffer& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(capacity_, other.capacity_);
        std::swap(size_, other.size_);
        std::swap(pool_, other.pool_);
    }

private:
    char* data_;
    size_t capacity_;
    size_t size_;
    memory::MemoryPool* pool_;
};

/// 文件操作
namespace file {

/// RAII 文件句柄（异常安全，自动关闭）
struct FileHandle {
    FILE* fp = nullptr;
    explicit FileHandle(FILE* f) noexcept : fp(f) {}
    ~FileHandle() { if (fp) std::fclose(fp); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    operator FILE*() const noexcept { return fp; }
};

/// 读取文件全部内容
inline std::string read_all(const std::string& path) {
    FileHandle file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    
    std::string content;
    content.resize(size);
    const size_t read = std::fread(content.data(), 1, size, file);
    
    if (static_cast<long>(read) != size) {
        throw std::runtime_error("Failed to read file: " + path);
    }
    
    return content;
}

/// 写入文件全部内容
inline void write_all(const std::string& path, const std::string& content) {
    FileHandle file(std::fopen(path.c_str(), "wb"));
    if (!file) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }
    
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    if (written != content.size()) {
        throw std::runtime_error("Failed to write file: " + path);
    }
}

/// 追加内容到文件
inline void append(const std::string& path, const std::string& content) {
    FileHandle file(std::fopen(path.c_str(), "ab"));
    if (!file) {
        throw std::runtime_error("Cannot open file for appending: " + path);
    }
    
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    if (written != content.size()) {
        throw std::runtime_error("Failed to append to file: " + path);
    }
}

/// 检查文件是否存在
inline bool exists(const std::string& path) {
    FileHandle file(std::fopen(path.c_str(), "r"));
    return file.fp != nullptr;
}

/// 获取文件大小
inline size_t file_size(const std::string& path) {
    FileHandle file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    
    return static_cast<size_t>(size);
}

}  // namespace file

}  // namespace ben_gear::base::io
