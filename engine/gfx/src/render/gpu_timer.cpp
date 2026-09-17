module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {
namespace {
constexpr log::log_category lc_{"gpu_timer"};

constexpr uint32 queries_per_frame = gpu_stage_count * 2;

auto mask_for_bits(uint32 bits) -> uint64 {
    if (bits == 0) {
        return 0;
    }
    if (bits >= 64) {
        return ~uint64{0};
    }
    return (uint64{1} << bits) - 1;
}

}  // namespace

gpu_timer::gpu_timer(
    vulkan_context& context, uint32 frames_in_flight
)
    : context_(&context), frames_in_flight_(frames_in_flight) {
    const uint32 valid_bits = context_->get_timestamp_valid_bits();
    const float32 period    = context_->get_timestamp_period();

    if (valid_bits == 0 || period <= 0.0f) {
        log::warn(lc_, "queue reports no usable timestamps, gpu timing is off");
        return;
    }

    period_ns_  = static_cast<float64>(period);
    valid_mask_ = mask_for_bits(valid_bits);

    pool_ = vk_must(
        context_->get_device().createQueryPool({
            .queryType  = vk::QueryType::eTimestamp,
            .queryCount = frames_in_flight_ * queries_per_frame,
        }),
        "create timestamp query pool"
    );

    frame_recorded_.assign(frames_in_flight_, 0);
    scratch_.resize(static_cast<std::size_t>(queries_per_frame) * 2);
    supported_       = true;
    stats_.supported = true;

    log::info(lc_, "gpu timestamps enabled, period {} ns, {} valid bits", period, valid_bits);
}

gpu_timer::~gpu_timer() {
    if (pool_ != nullptr) {
        context_->get_device().destroyQueryPool(pool_);
    }
}

auto gpu_timer::first_query_(
    uint32 frame_index
) const -> uint32 {
    return frame_index * queries_per_frame;
}

auto gpu_timer::reset(
    vk::CommandBuffer cmd, uint32 frame_index
) -> void {
    if (!supported_) {
        return;
    }

    recording_frame_ = frame_index;
    cmd.resetQueryPool(pool_, first_query_(frame_index), queries_per_frame);
    frame_recorded_[frame_index] = 1;
}

auto gpu_timer::begin(
    vk::CommandBuffer cmd, gpu_stage stage
) const -> void {
    if (!supported_) {
        return;
    }

    const uint32 query = first_query_(recording_frame_) + (static_cast<uint32>(stage) * 2);
    cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, pool_, query);
}

auto gpu_timer::end(
    vk::CommandBuffer cmd, gpu_stage stage
) const -> void {
    if (!supported_) {
        return;
    }

    const uint32 query = first_query_(recording_frame_) + (static_cast<uint32>(stage) * 2) + 1;
    cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, pool_, query);
}

auto gpu_timer::resolve(
    uint32 frame_index
) -> void {
    if (!supported_ || frame_recorded_[frame_index] == 0) {
        return;
    }

    constexpr auto flags =
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability;
    constexpr vk::DeviceSize stride = sizeof(uint64) * 2;

    const vk::Result result = context_->get_device().getQueryPoolResults(
        pool_,
        first_query_(frame_index),
        queries_per_frame,
        scratch_.size() * sizeof(uint64),
        scratch_.data(),
        stride,
        flags
    );

    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        vk_panic(result, "read timestamp query results");
    }

    for (uint32 stage = 0; stage < gpu_stage_count; ++stage) {
        const std::size_t begin_slot = static_cast<std::size_t>(stage) * 4;
        const std::size_t end_slot   = begin_slot + 2;

        if (scratch_[begin_slot + 1] == 0 || scratch_[end_slot + 1] == 0) {
            stats_.ms[stage] = 0.0f;
            continue;
        }

        const uint64 begin_ticks = scratch_[begin_slot] & valid_mask_;
        const uint64 end_ticks   = scratch_[end_slot] & valid_mask_;
        const uint64 delta       = (end_ticks - begin_ticks) & valid_mask_;

        stats_.ms[stage] = static_cast<float32>(static_cast<float64>(delta) * period_ns_ / 1.0e6);
    }
}

}  // namespace vw::gfx
