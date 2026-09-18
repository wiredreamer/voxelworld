module vw.core;

import std;

namespace vw {

auto latency_histogram::merge(const latency_histogram& other) -> void {
    for (uint32 i = 0; i < bucket_count; ++i) {
        buckets_[i] += other.buckets_[i];
    }
    count_ += other.count_;
    total_nanos_ += other.total_nanos_;
    max_us_ = std::max(max_us_, other.max_us_);
}

auto latency_histogram::clear() -> void {
    buckets_.fill(0);
    count_       = 0;
    total_nanos_ = 0;
    max_us_      = 0;
}

auto latency_histogram::summarize() const -> latency_summary {
    latency_summary out{};
    out.count    = count_;
    out.total_ms = static_cast<float32>(static_cast<float64>(total_nanos_) / 1.0e6);

    if (count_ == 0) {
        return out;
    }

    out.mean_us = static_cast<float32>(
        static_cast<float64>(total_nanos_) / 1000.0 / static_cast<float64>(count_)
    );
    out.max_us = static_cast<float32>(max_us_);

    const auto quantile_us = [this](float32 quantile) -> float32 {
        const auto rank = static_cast<uint64>(
            std::ceil(static_cast<float64>(quantile) * static_cast<float64>(count_))
        );
        const auto index = std::clamp<uint64>(rank, 1, count_) - 1;

        uint64 seen = 0;
        for (uint32 i = 0; i < bucket_count; ++i) {
            seen += buckets_[i];
            if (seen > index) {
                return static_cast<float32>(bucket_floor_us(i));
            }
        }
        return static_cast<float32>(max_us_);
    };

    out.p50_us = quantile_us(0.50F);
    out.p99_us = quantile_us(0.99F);

    return out;
}

}  // namespace vw
