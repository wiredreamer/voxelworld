#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using vw::float32;
using vw::float64;
using vw::latency_histogram;
using vw::uint32;
using vw::uint64;

namespace {

auto sorted_quantile(std::vector<uint32> samples, float32 quantile) -> float32 {
    std::ranges::sort(samples);
    const auto rank = static_cast<uint64>(
        std::ceil(static_cast<float64>(quantile) * static_cast<float64>(samples.size()))
    );
    const auto index = std::clamp<uint64>(rank, 1, samples.size()) - 1;
    return static_cast<float32>(samples[index]);
}

}  // namespace

TEST_CASE("latency_histogram is empty before anything is recorded", "[latency]") {
    const latency_histogram histogram;
    const auto summary = histogram.summarize();

    CHECK(summary.count == 0);
    CHECK(summary.total_ms == 0.0F);
    CHECK(summary.mean_us == 0.0F);
    CHECK(summary.p50_us == 0.0F);
    CHECK(summary.p99_us == 0.0F);
    CHECK(summary.max_us == 0.0F);
}

TEST_CASE("latency_histogram matches a sorted sample set below the exact limit", "[latency]") {
    latency_histogram histogram;
    std::vector<uint32> samples;

    std::mt19937 rng{20260918};
    std::uniform_int_distribution<uint32> micros{1, latency_histogram::exact_limit_us - 1};

    for (uint32 i = 0; i < 5000; ++i) {
        const auto value = micros(rng);
        samples.push_back(value);
        histogram.record(static_cast<uint64>(value) * 1000);
    }

    const auto summary = histogram.summarize();

    CHECK(summary.count == samples.size());
    CHECK(summary.p50_us == sorted_quantile(samples, 0.50F));
    CHECK(summary.p99_us == sorted_quantile(samples, 0.99F));
    CHECK(summary.max_us == sorted_quantile(samples, 1.00F));
}

TEST_CASE("latency_histogram stays within a step of the truth for column timings", "[latency]") {
    latency_histogram histogram;
    std::vector<uint32> samples;

    std::mt19937 rng{20260918};
    std::uniform_int_distribution<uint32> micros{3000, 12000};

    for (uint32 i = 0; i < 4000; ++i) {
        const auto value = micros(rng);
        samples.push_back(value);
        histogram.record(static_cast<uint64>(value) * 1000);
    }

    const auto summary = histogram.summarize();
    const auto step    = static_cast<float32>(latency_histogram::coarse_step_us);

    CHECK(summary.p50_us <= sorted_quantile(samples, 0.50F));
    CHECK(summary.p50_us > sorted_quantile(samples, 0.50F) - step);
    CHECK(summary.p99_us <= sorted_quantile(samples, 0.99F));
    CHECK(summary.p99_us > sorted_quantile(samples, 0.99F) - step);
    CHECK(summary.max_us == sorted_quantile(samples, 1.00F));
}

TEST_CASE("latency_histogram keeps an exact maximum past the exact limit", "[latency]") {
    latency_histogram histogram;

    histogram.record(1000);
    histogram.record(static_cast<uint64>(90'000) * 1000);

    const auto summary = histogram.summarize();

    CHECK(summary.count == 2);
    CHECK(summary.max_us == 90'000.0F);
    CHECK(summary.p99_us >= static_cast<float32>(latency_histogram::exact_limit_us));
    CHECK(summary.p99_us <= summary.max_us);
}

TEST_CASE("latency_histogram reports mean and total from recorded nanoseconds", "[latency]") {
    latency_histogram histogram;

    histogram.record(1'000'000);
    histogram.record(3'000'000);

    const auto summary = histogram.summarize();

    CHECK(summary.total_ms == 4.0F);
    CHECK(summary.mean_us == 2000.0F);
}

TEST_CASE("latency_histogram merges another histogram", "[latency]") {
    latency_histogram left;
    latency_histogram right;
    std::vector<uint32> samples;

    for (uint32 i = 1; i <= 100; ++i) {
        left.record(static_cast<uint64>(i) * 1000);
        samples.push_back(i);
    }
    for (uint32 i = 101; i <= 200; ++i) {
        right.record(static_cast<uint64>(i) * 1000);
        samples.push_back(i);
    }

    left.merge(right);
    const auto summary = left.summarize();

    CHECK(summary.count == 200);
    CHECK(summary.p50_us == sorted_quantile(samples, 0.50F));
    CHECK(summary.p99_us == sorted_quantile(samples, 0.99F));
    CHECK(summary.max_us == 200.0F);
}

TEST_CASE("latency_histogram clears back to empty", "[latency]") {
    latency_histogram histogram;

    histogram.record(5000);
    histogram.clear();

    const auto summary = histogram.summarize();

    CHECK(summary.count == 0);
    CHECK(summary.max_us == 0.0F);
}
