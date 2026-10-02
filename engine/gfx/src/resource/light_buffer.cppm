module;

#include <cstddef>

export module vw.gfx:resource.light_buffer;

import std;

import vw.core;
import :frames_in_flight;
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

static_assert(offsetof(point_light_data, position) == 0);
static_assert(offsetof(point_light_data, color) == 16);
static_assert(offsetof(point_light_data, intensity) == 32);
static_assert(offsetof(point_light_data, range) == 36);
static_assert(sizeof(point_light_data) == 48);

constexpr float32 blob_reach_falls = 3.0F;

struct blob_data {
    alignas(16) vec4f position_radius;

    alignas(16) vec4f params;

    alignas(16) vec4f cull_a;
    alignas(16) vec4f cull_b;
};

static_assert(offsetof(blob_data, position_radius) == 0);
static_assert(offsetof(blob_data, params) == 16);
static_assert(offsetof(blob_data, cull_a) == 32);
static_assert(offsetof(blob_data, cull_b) == 48);
static_assert(sizeof(blob_data) == 64);

class light_buffer {
public:
    using world_type = world;


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
    std::array<uint32, frames_in_flight> capacities_{};
    std::array<std::unique_ptr<storage_buffer>, frames_in_flight> lights_buffers_;
    std::array<vk::DescriptorSet, frames_in_flight> descriptor_sets_{};

    vk::DescriptorPool descriptor_pool_            = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_ = nullptr;

    uint32 lights_count_ = 0;
};

enum class cull_list : uint32 {
    sources = 0,
    blobs   = 1,
};

inline constexpr uint32 cull_list_count = 2;

struct light_cull_ubo {
    alignas(16) float32 view[16]{};

    alignas(4) float32 z_scale   = 0.0F;
    alignas(4) float32 z_bias    = 0.0F;
    alignas(4) float32 tile_size = 0.0F;
    alignas(4) float32 slices    = 0.0F;

    alignas(4) float32 near_depth = 0.0F;
    alignas(4) float32 far_depth  = 0.0F;
    alignas(4) float32 proj_x     = 0.0F;
    alignas(4) float32 proj_y     = 0.0F;

    alignas(4) float32 screen_width  = 0.0F;
    alignas(4) float32 screen_height = 0.0F;
    alignas(4) float32 tiles_x       = 0.0F;
    alignas(4) float32 tiles_y       = 0.0F;

    alignas(4) uint32 cap           = 0;
    alignas(4) uint32 shape_count   = 0;
    alignas(4) uint32 cluster_count = 0;
    alignas(4) cull_list list       = cull_list::sources;

    alignas(4) uint32 orthographic = 0;
};

static_assert(offsetof(light_cull_ubo, z_scale) == 64);
static_assert(offsetof(light_cull_ubo, near_depth) == 80);
static_assert(offsetof(light_cull_ubo, screen_width) == 96);
static_assert(offsetof(light_cull_ubo, cap) == 112);
static_assert(offsetof(light_cull_ubo, list) == 124);
static_assert(offsetof(light_cull_ubo, orthographic) == 128);
static_assert(sizeof(light_cull_ubo) == 144);

class blob_buffer {
public:
    using world_type = world;


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
    std::array<uint32, frames_in_flight> capacities_{};
    std::array<std::unique_ptr<storage_buffer>, frames_in_flight> buffers_;
    std::array<vk::DescriptorSet, frames_in_flight> sets_{};
};
}  // namespace vw::gfx
