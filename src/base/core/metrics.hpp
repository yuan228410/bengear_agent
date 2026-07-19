#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ben_gear::base {

/// 标签键值对
using MetricLabels = std::unordered_map<std::string, std::string>;

/// 指标收集器接口 — 支持 Prometheus / OpenTelemetry 等后端
///
/// 注册到 ServiceRegistry，运行时动态替换实现。
/// 默认 NoopMetricsCollector 所有方法空操作，零开销。
class IMetricsCollector {
public:
    virtual ~IMetricsCollector() = default;

    /// 计数器：累计值（如请求总数、错误总数）
    virtual void counter(std::string_view name,
                         uint64_t value = 1,
                         MetricLabels labels = {}) = 0;

    /// 仪表盘：可增可减（如当前连接数、内存使用）
    virtual void gauge(std::string_view name,
                       double value,
                       MetricLabels labels = {}) = 0;

    /// 直方图：延迟/大小分布
    virtual void histogram(std::string_view name,
                           double value,
                           MetricLabels labels = {}) = 0;

    /// 耗时统计（自动转为直方图）
    virtual void timing(std::string_view name,
                        std::chrono::microseconds duration,
                        MetricLabels labels = {}) = 0;
};

/// 空实现 — 编译期零开销
class NoopMetricsCollector : public IMetricsCollector {
public:
    void counter(std::string_view, uint64_t, MetricLabels) override {}
    void gauge(std::string_view, double, MetricLabels) override {}
    void histogram(std::string_view, double, MetricLabels) override {}
    void timing(std::string_view, std::chrono::microseconds, MetricLabels) override {}
};

/// 耗时统计 RAII 辅助
class ScopedTimer {
public:
    ScopedTimer(IMetricsCollector* collector,
                std::string_view name,
                MetricLabels labels = {})
        : collector_(collector),
          name_(name),
          labels_(std::move(labels)),
          start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        if (collector_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_);
            collector_->timing(name_, elapsed, std::move(labels_));
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    IMetricsCollector* collector_;
    std::string name_;
    MetricLabels labels_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ben_gear::base
