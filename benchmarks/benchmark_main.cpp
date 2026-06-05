#include <glog/logging.h>

#include "ben_gear/config/loader.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/container/string.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

class NullSink final : public ben_gear::log::Sink {
public:
    void write(const ben_gear::log::Record&, std::string_view formatted) override {
        bytes += formatted.size();
    }

    std::size_t bytes = 0;
};

struct BenchmarkResult {
    std::string name;
    std::size_t iterations = 0;
    double total_ms = 0.0;
    double ns_per_op = 0.0;
    double ops_per_sec = 0.0;
};

BenchmarkResult make_result(std::string name, std::size_t iterations, Clock::time_point start, Clock::time_point end) {
    const auto total_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return {
        std::move(name),
        iterations,
        total_ns / 1'000'000.0,
        total_ns / static_cast<double>(iterations),
        static_cast<double>(iterations) * 1'000'000'000.0 / total_ns,
    };
}

template <typename Function>
BenchmarkResult run_benchmark(std::string name, std::size_t iterations, Function function) {
    volatile std::size_t sink = 0;
    const auto start = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        sink += static_cast<std::size_t>(function(index));
    }
    const auto end = Clock::now();
    (void)sink;
    return make_result(std::move(name), iterations, start, end);
}

template <typename Function>
BenchmarkResult run_timed_benchmark(std::string name, std::size_t iterations, Function function) {
    const auto start = Clock::now();
    function();
    const auto end = Clock::now();
    return make_result(std::move(name), iterations, start, end);
}

ben_gear::net::Task<int> immediate_task(int value) {
    co_return value + 1;
}

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(28) << result.name
              << " iterations=" << std::right << std::setw(10) << result.iterations
              << " total_ms=" << std::fixed << std::setprecision(3) << std::setw(10) << result.total_ms
              << " ns/op=" << std::fixed << std::setprecision(2) << std::setw(10) << result.ns_per_op
              << " ops/s=" << std::fixed << std::setprecision(0) << result.ops_per_sec << '\n';
}

std::filesystem::path write_config_fixture() {
    const auto path = std::filesystem::temp_directory_path() / "bengear-benchmark.conf";
    std::ofstream out(path);
    out << "provider=openai\n"
        << "api_key=benchmark-key\n"
        << "base_url=https://api.openai.com\n"
        << "model=gpt-4o-mini\n"
        << "max_tokens=1024\n"
        << "temperature=0.2\n";
    return path;
}

}  // namespace

int main(int /*argc*/, char** argv) {
    google::InitGoogleLogging(argv[0]);
    std::vector<BenchmarkResult> results;

    const std::string text = "  BenGear Agentic AI benchmark payload with spaces and Mixed CASE  ";
    results.push_back(run_benchmark("base::utils::trim", 2'000'000, [&](std::size_t) {
        return ben_gear::base::utils::trim(text).size();
    }));

    results.push_back(run_benchmark("base::utils::to_lower", 1'000'000, [&](std::size_t) {
        return ben_gear::base::utils::to_lower(text).size();
    }));

    const std::string json_payload = "BenGear says: \"hello\"\\path\nAgentic AI\tbenchmark";
    results.push_back(run_benchmark("base::utils::json_string", 500'000, [&](std::size_t) {
        return ben_gear::json_string(json_payload).size();
    }));

    const std::string response = "{\"id\":\"abc\",\"content\":\"fallback\",\"text\":\"hello benchmark\"}";
    results.push_back(run_benchmark("base::utils::parse_json", 1'000'000, [&](std::size_t) {
        std::string error;
        auto json = ben_gear::parse_json(response, error);
        if (error.empty()) {
            if (auto text = ben_gear::get_json_value<std::string>(json, "text")) {
                return text->size();
            }
        }
        return std::size_t(0);
    }));

    const auto config_file = write_config_fixture();
    results.push_back(run_benchmark("config.read_apply", 100'000, [&](std::size_t) {
        auto values = ben_gear::config::read_key_value_file(config_file);
        ben_gear::config::Settings settings;
        ben_gear::config::apply_values(settings, values);
        return settings.model.size() + settings.api_key.size();
    }));
    std::filesystem::remove(config_file);

    results.push_back(run_benchmark("net.task_resume", 500'000, [&](std::size_t index) {
        auto task = immediate_task(static_cast<int>(index));
        task.resume();
        return static_cast<std::size_t>(task.result());
    }));

    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    results.push_back(run_benchmark("net.event_loop_poll0", 100'000, [&](std::size_t) {
        loop.run_once(std::chrono::milliseconds{0});
        return 1;
    }));

    ben_gear::log::Logger disabled_logger(ben_gear::log::Level::off, {});
    results.push_back(run_benchmark("log.disabled", 2'000'000, [&](std::size_t index) {
        disabled_logger.log(ben_gear::log::Level::debug, "disabled log " + std::to_string(index));
        return 1;
    }));

    auto manager_sink = std::make_shared<NullSink>();
    auto manager_logger = std::make_shared<ben_gear::log::Logger>(ben_gear::log::Level::info, ben_gear::log::SinkList{manager_sink}, 1'000'000);
    ben_gear::log::LogManager::set_logger(manager_logger);
    results.push_back(run_timed_benchmark("log.manager_enqueue", 300'000, [&] {
        for (std::size_t index = 0; index < 300'000; ++index) {
            ben_gear::log::info("manager benchmark " + std::to_string(index));
        }
    }));
    ben_gear::log::LogManager::set_logger({});

    results.push_back(run_timed_benchmark("log.enqueue_null", 300'000, [&] {
        auto null_sink = std::make_shared<NullSink>();
        ben_gear::log::Logger logger(ben_gear::log::Level::info, {null_sink}, 1'000'000);
        for (std::size_t index = 0; index < 300'000; ++index) {
            logger.log(ben_gear::log::Level::info, "enqueue benchmark " + std::to_string(index));
        }
        logger.flush();
    }));

    const auto log_file = std::filesystem::temp_directory_path() / "bengear-log-benchmark.log";
    results.push_back(run_timed_benchmark("log.enqueue_file", 100'000, [&] {
        ben_gear::log::Logger logger(ben_gear::log::Level::info, {std::make_shared<ben_gear::log::FileSink>(log_file)}, 200'000);
        for (std::size_t index = 0; index < 100'000; ++index) {
            logger.log(ben_gear::log::Level::info, "file benchmark " + std::to_string(index));
        }
        logger.flush();
    }));
    std::filesystem::remove(log_file);

    results.push_back(run_timed_benchmark("log.enqueue_contended", 200'000, [&] {
        auto null_sink = std::make_shared<NullSink>();
        ben_gear::log::Logger logger(ben_gear::log::Level::info, {null_sink}, 400'000);
        constexpr std::size_t threads = 4;
        constexpr std::size_t per_thread = 50'000;
        std::vector<std::thread> workers;
        workers.reserve(threads);
        for (std::size_t worker = 0; worker < threads; ++worker) {
            workers.emplace_back([&logger, worker] {
                for (std::size_t index = 0; index < per_thread; ++index) {
                    logger.log(ben_gear::log::Level::info, "thread " + std::to_string(worker) + " log " + std::to_string(index));
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        logger.flush();
    }));

    std::cout << "BenGear benchmark results\n";
    std::cout << "========================\n";
    for (const auto& result : results) {
        print_result(result);
    }
}
