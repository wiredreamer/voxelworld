module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {
namespace {
constexpr log::log_category lc_{"frame_probe"};

constexpr auto counted_statistics =
    vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
    vk::QueryPipelineStatisticFlagBits::eClippingPrimitives |
    vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations;

constexpr std::size_t counted_statistic_count = 3;

constexpr vk::DeviceSize counts_bytes =
    vk::DeviceSize{combined_buffer::cull_region_count} * sizeof(uint32);

}  // namespace

frame_probe::frame_probe(
    vulkan_context& context, uint32 frames_in_flight
)
    : context_(&context) {
    frames_.resize(frames_in_flight);
    for (frame_state& frame : frames_) {
        frame.counts = std::make_unique<storage_buffer>(
            *context_, vk::DeviceSize{most_buffers} * counts_bytes,
            vk::BufferUsageFlagBits::eTransferDst
        );
    }

    hidden_pool_ = vk_must(
        context_->get_device().createQueryPool({
            .queryType  = vk::QueryType::eOcclusion,
            .queryCount = frames_in_flight,
        }),
        "create hidden samples query pool"
    );

    if (!context_->counts_pipeline_statistics()) {
        log::warn(lc_, "the device does not count pipeline statistics");
        return;
    }

    pool_ = vk_must(
        context_->get_device().createQueryPool({
            .queryType          = vk::QueryType::ePipelineStatistics,
            .queryCount         = frames_in_flight,
            .pipelineStatistics = counted_statistics,
        }),
        "create pipeline statistics query pool"
    );
}

frame_probe::~frame_probe() {
    if (pool_ != nullptr) {
        context_->get_device().destroyQueryPool(pool_);
    }
    if (hidden_pool_ != nullptr) {
        context_->get_device().destroyQueryPool(hidden_pool_);
    }
}

auto frame_probe::reset(
    vk::CommandBuffer cmd, uint32 frame_index
) -> void {
    frame_state& frame = frames_[frame_index];

    recording_frame_       = frame_index;
    recording_             = enabled_ || watching_hidden_;
    frame.queried          = false;
    frame.hidden_queried   = false;
    frame.hidden_reset     = false;

    if (watching_hidden_) {
        cmd.resetQueryPool(hidden_pool_, frame_index, 1);
        frame.hidden_reset = true;
    }

    frame.buffers_copied   = 0;
    frame.commands_offered = 0;

    if (!enabled_ || pool_ == nullptr) {
        return;
    }

    cmd.resetQueryPool(pool_, frame_index, 1);
    frame.queried = true;
}

auto frame_probe::copy_cull_counts(
    vk::CommandBuffer cmd, std::span<const std::unique_ptr<combined_buffer>> buffers,
    uint32 frame_index
) -> void {
    if (!recording_) {
        return;
    }

    frame_state& frame = frames_[frame_index];

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        },
        nullptr,
        nullptr
    );

    for (const auto& buffer : buffers) {
        if (buffer->get_instance_count() == 0 || frame.buffers_copied == most_buffers) {
            continue;
        }

        cmd.copyBuffer(
            buffer->get_count_buffer(), frame.counts->get_buffer(),
            vk::BufferCopy{
                .srcOffset = 0,
                .dstOffset = vk::DeviceSize{frame.buffers_copied} * counts_bytes,
                .size      = counts_bytes,
            }
        );

        frame.commands_offered += buffer->get_draw_command_count();
        ++frame.buffers_copied;
    }
}

auto frame_probe::begin(
    vk::CommandBuffer cmd
) const -> void {
    if (frames_[recording_frame_].queried) {
        cmd.beginQuery(pool_, recording_frame_, {});
    }
}

auto frame_probe::end(
    vk::CommandBuffer cmd
) const -> void {
    if (frames_[recording_frame_].queried) {
        cmd.endQuery(pool_, recording_frame_);
    }
}

auto frame_probe::begin_hidden(
    vk::CommandBuffer cmd
) -> void {
    frame_state& frame = frames_[recording_frame_];
    if (!frame.hidden_reset) {
        return;
    }

    cmd.beginQuery(
        hidden_pool_, recording_frame_,
        context_->counts_samples_exactly() ? vk::QueryControlFlagBits::ePrecise
                                           : vk::QueryControlFlags{}
    );
    frame.hidden_queried = true;
}

auto frame_probe::end_hidden(
    vk::CommandBuffer cmd
) const -> void {
    if (frames_[recording_frame_].hidden_queried) {
        cmd.endQuery(hidden_pool_, recording_frame_);
    }
}

auto frame_probe::resolve(
    uint32 frame_index
) -> void {
    frame_state& frame = frames_[frame_index];

    stats_ = frame_probe_stats{};

    if (frame.buffers_copied > 0) {
        std::array<uint32, std::size_t{most_buffers} * combined_buffer::cull_region_count> drawn{};
        frame.counts->copy_to(drawn.data(), vk::DeviceSize{frame.buffers_copied} * counts_bytes);

        stats_.cull_counted     = true;
        stats_.commands_offered = frame.commands_offered;
        for (uint32 buffer = 0; buffer < frame.buffers_copied; ++buffer) {
            const uint32* regions = &drawn[std::size_t{buffer} * combined_buffer::cull_region_count];
            for (uint32 ring = 0; ring < combined_buffer::cull_ring_count; ++ring) {
                stats_.commands_drawn += regions[ring];
            }
            stats_.commands_hidden += regions[combined_buffer::cull_hidden_region];
        }
    }

    if (frame.hidden_queried) {
        std::array<uint64, 2> shown{};

        const vk::Result hidden_result = context_->get_device().getQueryPoolResults(
            hidden_pool_, frame_index, 1, shown.size() * sizeof(uint64), shown.data(),
            shown.size() * sizeof(uint64),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability
        );
        if (hidden_result != vk::Result::eSuccess && hidden_result != vk::Result::eNotReady) {
            vk_panic(hidden_result, "read hidden samples");
        }
        if (shown[1] != 0) {
            stats_.hidden_counted       = true;
            stats_.hidden_samples_shown = shown[0];
        }
    }

    if (!frame.queried) {
        return;
    }

    std::array<uint64, counted_statistic_count + 1> counted{};

    const vk::Result result = context_->get_device().getQueryPoolResults(
        pool_,
        frame_index,
        1,
        counted.size() * sizeof(uint64),
        counted.data(),
        counted.size() * sizeof(uint64),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability
    );

    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        vk_panic(result, "read pipeline statistics");
    }
    if (counted[counted_statistic_count] == 0) {
        return;
    }

    stats_.pipeline_counted     = true;
    stats_.vertex_invocations   = counted[0];
    stats_.clipped_primitives   = counted[1];
    stats_.fragment_invocations = counted[2];
}

}  // namespace vw::gfx
