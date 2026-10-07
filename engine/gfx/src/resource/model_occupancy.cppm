export module vw.gfx:resource.model_occupancy;

import std;

import vw.core;
import :frames_in_flight;
import vw.asset;
import :gpu_buffers;
import vulkan;

export namespace vw::gfx {

// см. docs/rendering.md#затенение-углов-во-фрагменте
struct instance_corners {
    float32 packed_size = -1.0F;
    float32 word_offset = 0.0F;

    [[nodiscard]] static constexpr auto baked() -> instance_corners {
        return {};
    }

    [[nodiscard]] static constexpr auto on_world_grid() -> instance_corners {
        return {.packed_size = 0.0F, .word_offset = 0.0F};
    }

    [[nodiscard]] constexpr auto has_volume() const -> bool {
        return packed_size > 0.5F;
    }

    auto operator==(const instance_corners&) const -> bool = default;
};

struct model_occupancy_stats {
    uint32 volumes    = 0;
    uint32 words_used = 0;
    uint32 refused    = 0;
};

class model_occupancy_buffer {
public:
    static constexpr uint32 capacity_words = uint32{2} << 20;
    static constexpr int32 longest_side    = 128;

    explicit model_occupancy_buffer(vulkan_context& context);
    ~model_occupancy_buffer();

    model_occupancy_buffer(const model_occupancy_buffer&)                    = delete;
    auto operator=(const model_occupancy_buffer&) -> model_occupancy_buffer& = delete;
    model_occupancy_buffer(model_occupancy_buffer&&)                         = delete;
    auto operator=(model_occupancy_buffer&&) -> model_occupancy_buffer&      = delete;

    [[nodiscard]] auto acquire(const asset::model& voxels) -> instance_corners;
    [[nodiscard]] auto refresh(const asset::model& voxels) -> instance_corners;

    // см. docs/rendering.md#трава
    [[nodiscard]] auto keep_copy(const asset::model& voxels) -> instance_corners;
    auto release(uint32 model_index) -> void;

    auto next_frame() -> void;

    [[nodiscard]] auto get_buffer() const -> vk::Buffer {
        return words_->get_buffer();
    }

    [[nodiscard]] static constexpr auto byte_size() -> vk::DeviceSize {
        return vk::DeviceSize{capacity_words} * sizeof(uint32);
    }

    [[nodiscard]] auto get_stats() const -> const model_occupancy_stats& {
        return stats_;
    }

private:
    struct volume {
        uint32 offset = 0;
        uint32 words  = 0;
        vec3i size{};
        uint32 holders = 0;
    };

    struct freed_range {
        uint32 offset   = 0;
        uint32 words    = 0;
        uint64 ripe_at  = 0;
    };

    [[nodiscard]] static auto words_for_(vec3i size) -> uint32;
    [[nodiscard]] static auto corners_of_(const volume& held) -> instance_corners;
    [[nodiscard]] auto take_range_(uint32 words) -> std::optional<uint32>;
    auto give_back_(const volume& held) -> void;
    auto write_(const asset::model& voxels, const volume& held) -> void;

    std::unique_ptr<buffer> words_;
    uint32* mapped_    = nullptr;
    uint32 high_water_ = 0;
    uint64 frame_      = 0;

    std::unordered_map<uint32, volume> volumes_;
    std::vector<freed_range> freed_;
    model_occupancy_stats stats_;
};

}  // namespace vw::gfx
