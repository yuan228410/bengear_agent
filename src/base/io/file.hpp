#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace ben_gear::base::io {

inline std::string read_all_stdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

}  // namespace ben_gear::base::io

namespace ben_gear {
using base::io::read_all_stdin;
}  // namespace ben_gear
