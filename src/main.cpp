#include "cli/app.hpp"
#include "cli/repl/terminal_io.hpp"
#include "log/logger.hpp"
#include "platform/crash_handler.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    ben_gear::base::platform::compat::init_console_utf8();

    // 注册 crash handler，注入终端恢复回调
    ben_gear::base::platform::install_crash_handler([]() {
        ben_gear::cli::restore_terminal_on_crash();
    });

    try {
        return ben_gear::cli::run_cli(argc, argv);
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
