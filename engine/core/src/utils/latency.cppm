export module vw.core:latency;

import std;

import :types;

export namespace vw {

struct latency_summary {
    uint64 count     = 0;
    float32 total_ms = 0.0F;
    float32 mean_us  = 0.0F;
    float32 p50_us   = 0.0F;
    float32 p99_us   = 0.0F;
    float32 max_us   = 0.0F;
};

class latency_histogram final {
public:
    static constexpr uint32 exact_limit_us    = 4096;
    static constexpr uint32 coarse_limit_us   = 65536;
    static constexpr uint32 coarse_step_us    = 16;
    static constexpr uint32 coarse_buckets    = (coarse_limit_us - exact_limit_us) / coarse_step_us;
    static constexpr uint32 doubling_buckets  = 32;
    static constexpr uint32 bucket_count      = exact_limit_us + coarse_buckets + doubling_buckets;

    auto record(uint64 nanos) -> void {
        const auto micros = static_cast<uint32>(nanos / 1000);
        ++buckets_[bucket_of(micros)];
        ++count_;
        total_nanos_ += nanos;
        max_us_ = std::max(max_us_, micros);
    }

    auto merge(const latency_histogram& other) -> void;

    auto clear() -> void;

    [[nodiscard]] auto summarize() const -> latency_summary;

    [[nodiscard]] auto count() const -> uint64 {
        return count_;
    }

    [[nodiscard]] auto total_nanos() const -> uint64 {
        return total_nanos_;
    }

private:
    [[nodiscard]] static auto bucket_of(uint32 micros) -> uint32 {
        if (micros < exact_limit_us) {
            return micros;
        }
        if (micros < coarse_limit_us) {
            return exact_limit_us + ((micros - exact_limit_us) / coarse_step_us);
        }
        const auto doublings = static_cast<uint32>(std::bit_width(micros / coarse_limit_us)) - 1;
        return exact_limit_us + coarse_buckets + std::min(doublings, doubling_buckets - 1);
    }

    [[nodiscard]] static auto bucket_floor_us(uint32 index) -> uint32 {
        if (index < exact_limit_us) {
            return index;
        }
        if (index < exact_limit_us + coarse_buckets) {
            return exact_limit_us + ((index - exact_limit_us) * coarse_step_us);
        }
        return coarse_limit_us << (index - exact_limit_us - coarse_buckets);
    }

    std::array<uint32, bucket_count> buckets_{};
    uint64 count_       = 0;
    uint64 total_nanos_ = 0;
    uint32 max_us_      = 0;
};

}  // namespace vw
