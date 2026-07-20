#include "net/io_context.hpp"

namespace ben_gear::net {

IoContext::IoContext(const std::string& name)
    : loop_(std::make_unique<EventLoop>())
    , name_(name) {
    thread_ = std::thread([this] {
        log::info_fmt("IoContext [{}] thread started", name_);
        loop_->run();
        log::info_fmt("IoContext [{}] thread stopped", name_);
    });
}

IoContext::~IoContext() {
    if (thread_.joinable()) {
        loop_->drain();
        thread_.join();
    }
}

EventLoop& IoContext::loop() { return *loop_; }
const EventLoop& IoContext::loop() const { return *loop_; }

}  // namespace ben_gear::net
