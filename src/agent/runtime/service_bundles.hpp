#pragma once

#include <memory>

namespace ben_gear {

namespace base::concurrency { class ThreadPool; }
namespace net { class IoContext; }

}  // namespace ben_gear

namespace ben_gear::agent::runtime {


struct InfrastructureServices {
    std::shared_ptr<base::concurrency::ThreadPool> core_pool;
    std::shared_ptr<net::IoContext> io_context;
    std::shared_ptr<net::IoContext> wf_context;
    std::shared_ptr<net::IoContext> util_context;
};

}  // namespace ben_gear::agent::runtime
