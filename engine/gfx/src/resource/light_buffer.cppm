export module vw.gfx:resource.light_buffer;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import :gpu_buffers;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {


class vulkan_context;

struct point_light_data {
    alignas(16) vec4f position;
    alignas(16) vec4f color;
    alignas(4) float32 intensity;
    alignas(4) float32 range;
};

constexpr float32 blob_reach_falls = 3.0F;

struct blob_data {
    alignas(16) vec4f position_radius;

    alignas(16) vec4f params;

    alignas(16) vec4f cull_a;
    alignas(16) vec4f cull_b;
};

class light_buffer {
public:
    using world_type = world;

    static constexpr uint32 max_frames_in_flight = 2;

    explicit light_buffer(
        vulkan_context& context,
        deletion_queue& deletion,
        vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout descriptor_set_layout
    );
    ~light_buffer();

    auto update(
        world_type& world, const spatial::frustum& frustum, const vec3f& eye, uint32 frame_index
    ) -> void;

    [[nodiscard]] auto get_descriptor_set(uint32 frame_index) const -> vk::DescriptorSet;
    [[nodiscard]] auto is_empty() const -> bool;
    [[nodiscard]] auto get_lights_count() const -> uint32;

    [[nodiscard]] auto get_lights() const -> std::span<const point_light_data> {
        return lights_;
    }

    static constexpr uint32 no_cap = std::numeric_limits<uint32>::max();

    [[nodiscard]] auto get_max_visible() -> uint32& {
        return max_visible_;
    }

private:
    auto expand_buffer_if_needed_(uint32 frame_index, uint32 required_count) -> void;
    auto update_descriptor_set_(uint32 frame_index) -> void;

    static constexpr uint32 default_capacity_ = 64;

    uint32 max_visible_ = no_cap;

    vulkan_context* context_;
    deletion_queue* deletion_;
    std::vector<point_light_data> lights_;
    std::array<uint32, max_frames_in_flight> capacities_{};
    std::array<std::unique_ptr<storage_buffer>, max_frames_in_flight> lights_buffers_;
    std::array<vk::DescriptorSet, max_frames_in_flight> descriptor_sets_{};

    vk::DescriptorPool descriptor_pool_            = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_ = nullptr;

    uint32 lights_count_ = 0;
};

struct light_cull_ubo {
    alignas(16) float32 view[16]{};
    alignas(16) vec4f cluster_params{};
    alignas(16) vec4f cluster_extent{};
    alignas(16) vec4f screen_dims{};
    alignas(16) vec4<uint32> cull_dims{};
};

class blob_buffer {
public:
    using world_type = world;

    static constexpr uint32 max_frames_in_flight = 2;

    blob_buffer(
        vulkan_context& context,
        deletion_queue& deletion,
        std::span<const vk::DescriptorSet> sets
    );

    auto update(world_type& world, uint32 frame_index) -> void;

    [[nodiscard]] auto get_blobs() const -> std::span<const blob_data> {
        return blobs_;
    }

    [[nodiscard]] auto get_count() const -> uint32 {
        return static_cast<uint32>(blobs_.size());
    }

private:
    auto expand_buffer_if_needed_(uint32 frame_index, uint32 required_count) -> void;
    auto write_binding_(uint32 frame_index) -> void;

    static constexpr uint32 default_capacity_ = 32;

    vulkan_context* context_;
    deletion_queue* deletion_;
    std::vector<blob_data> blobs_;
    std::array<uint32, max_frames_in_flight> capacities_{};
    std::array<std::unique_ptr<storage_buffer>, max_frames_in_flight> buffers_;
    std::array<vk::DescriptorSet, max_frames_in_flight> sets_{};
};
}  // namespace vw::gfx
