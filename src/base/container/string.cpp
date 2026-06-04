#include "ben_gear/base/container/string.hpp"
#include <cstdlib>
#include <cstring>

namespace ben_gear::base::container {

void String::reserve(size_t new_capacity) {
    const size_t current_size = size();
    
    if (new_capacity <= sso_capacity) {
        // 如果新容量在 SSO 范围内，且当前是大字符串，需要转换
        if (!is_small_) {
            char* old_ptr = large_.ptr;
            std::memcpy(small_.data, old_ptr, current_size + 1);
            small_.size = static_cast<uint8_t>(current_size);
            is_small_ = true;
            ::operator delete(old_ptr);
        }
        return;
    }
    
    // 需要堆分配
    char* new_ptr = static_cast<char*>(::operator new(new_capacity + 1));
    
    if (is_small_) {
        // 从 SSO 转换到堆
        std::memcpy(new_ptr, small_.data, current_size + 1);
    } else {
        // 已在堆上，需要重新分配
        std::memcpy(new_ptr, large_.ptr, current_size + 1);
        ::operator delete(large_.ptr);
    }
    
    large_.ptr = new_ptr;
    large_.size = current_size;
    large_.capacity = new_capacity;
    is_small_ = false;
}

void String::resize(size_t new_size, char fill) {
    const size_t current_size = size();
    
    if (new_size == current_size) {
        return;
    }
    
    if (new_size < current_size) {
        // 缩小
        if (is_small_) {
            small_.data[new_size] = '\0';
            small_.size = static_cast<uint8_t>(new_size);
        } else {
            large_.ptr[new_size] = '\0';
            large_.size = new_size;
        }
    } else {
        // 扩大
        if (new_size > capacity()) {
            reserve(new_size * 2);
        }
        
        char* ptr = const_cast<char*>(data());
        std::memset(ptr + current_size, fill, new_size - current_size);
        ptr[new_size] = '\0';
        
        if (is_small_) {
            small_.size = static_cast<uint8_t>(new_size);
        } else {
            large_.size = new_size;
        }
    }
}

}  // namespace ben_gear::base::container
