export module vw.gfx:resource.light_grid;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import :camera;
import :gpu_buffers;
import :resource.light_buffer;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {

enum class cluster_readback_level : uint8 {
    off,
    counts,
    full,
};

enum class cull_list : uint32 {
    sources = 0,
    blobs   = 1,
};

inline constexpr uint32 cull_list_count = 2;

struct cluster_readback {
    cull_list kind = cull_list::sources;
    spatial::cluster_grid grid{};
    uint32 cap = 0;

    std::vector<spatial::view_capsule> columns;

    // Длиной cluster_count + 1, последняя запись — счёт переполнений.
    std::vector<uint32> counts;

    std::vector<uint32> indices;
};

class light_grid {
public:
    static constexpr uint32 max_frames_in_flight = 2;

    light_grid(
        vulkan_context& context,
        deletion_queue& deletion,
        vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout light_set_layout,
        std::span<const vk::DescriptorSet> light_sets
    );
    ~light_grid();

    light_grid(const light_grid&)                    = delete;
    auto operator=(const light_grid&) -> light_grid& = delete;
    light_grid(light_grid&&)                         = delete;
    auto operator=(light_grid&&) -> light_grid&      = delete;

    auto set_grid(const spatial::cluster_grid& grid, uint32 cap, uint32 blob_cap) -> void;

    auto dispatch(
        vk::CommandBuffer cmd,
        vk::DescriptorSet light_set,
        const mat4f& view,
        std::span<const point_light_data> lights,
        std::span<const blob_data> blobs,
        uint32 frame_index
    ) -> void;

    auto set_readback(cluster_readback_level level) -> void;

    [[nodiscard]] auto take_readback(cull_list kind) -> std::optional<cluster_readback>;

    [[nodiscard]] auto get_cluster_count() const -> uint32;
    [[nodiscard]] auto get_cap() const -> uint32;

private:
    struct list_frame {
        std::unique_ptr<device_storage_buffer> counts;
        std::unique_ptr<device_storage_buffer> indices;
        std::unique_ptr<storage_buffer> counts_host;
        std::unique_ptr<storage_buffer> indices_host;
        cluster_readback pending{};
        bool pending_valid = false;
    };

    auto create_pipeline_() -> void;
    auto create_params_ubos_() -> void;
    auto rebuild_frame_(uint32 frame_index) -> void;
    auto rebuild_list_(cull_list kind, uint32 frame_index) -> void;
    auto rebuild_mirrors_(cull_list kind, uint32 frame_index) -> void;
    auto harvest_(cull_list kind, uint32 frame_index) -> void;
    auto record_(
        vk::CommandBuffer cmd, vk::DescriptorSet light_set, cull_list kind, uint32 sphere_count,
        uint32 frame_index
    ) -> void;
    auto write_params_(cull_list kind, const mat4f& view, uint32 sphere_count, uint32 frame_index)
        -> void;
    auto snapshot_(cull_list kind, uint32 frame_index) -> cluster_readback&;

    [[nodiscard]] auto cap_of(cull_list kind) const -> uint32;

    [[nodiscard]] auto list_(cull_list kind, uint32 frame_index) -> list_frame& {
        return lists_[static_cast<uint32>(kind)][frame_index];
    }

    [[nodiscard]] static auto params_slot_(cull_list kind, uint32 frame_index) -> uint32 {
        return (frame_index * cull_list_count) + static_cast<uint32>(kind);
    }

    [[nodiscard]] auto counts_size_() const -> vk::DeviceSize;
    [[nodiscard]] auto indices_size_(cull_list kind) const -> vk::DeviceSize;

    vulkan_context* context_;
    deletion_queue* deletion_;
    vk::DescriptorPool descriptor_pool_ = nullptr;

    std::unique_ptr<shader> compute_shader_;
    vk::Pipeline compute_pipeline_                        = nullptr;
    vk::PipelineLayout compute_pipeline_layout_           = nullptr;
    vk::DescriptorSetLayout params_descriptor_set_layout_ = nullptr;
    vk::DescriptorSetLayout light_set_layout_             = nullptr;

    static constexpr uint32 params_slots_ = max_frames_in_flight * cull_list_count;

    std::array<std::unique_ptr<uniform_buffer>, params_slots_> params_ubos_;
    std::array<vk::DescriptorSet, params_slots_> params_descriptor_sets_{};

    std::array<vk::DescriptorSet, max_frames_in_flight> light_sets_{};
    std::array<std::array<list_frame, max_frames_in_flight>, cull_list_count> lists_{};

    cluster_readback_level readback_ = cluster_readback_level::off;
    std::array<std::optional<cluster_readback>, cull_list_count> ready_{};

    std::array<uint64, max_frames_in_flight> built_{};
    uint64 generation_ = 1;

    spatial::cluster_grid grid_{};
    uint32 cap_ = 32;

    uint32 blob_cap_      = 16;
    uint32 cluster_count_ = 0;
};

}  // namespace vw::gfx
