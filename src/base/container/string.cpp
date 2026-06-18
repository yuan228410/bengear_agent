#include "ben_gear/base/container/string.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ben_gear::base::container {

void String::reserve(size_t new_capacity) {
    const size_t current_size = size();

    // reserve() is grow-only. Shrinking a large string into SSO here can
    // overflow the small buffer when current_size > sso_capacity.
    if (new_capacity <= capacity()) {
        return;
    }
    if (new_capacity == std::numeric_limits<size_t>::max()) {
        throw std::length_error("String reserve capacity overflow");
    }

    char* new_ptr = static_cast<char*>(::operator new(new_capacity + 1));

    if (is_small_) {
        std::memcpy(new_ptr, small_.data, current_size + 1);
    } else {
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
            reserve(growth_capacity(new_size, capacity()));
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
