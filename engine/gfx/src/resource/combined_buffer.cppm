module;

#include <cstddef>

export module vw.gfx:resource.combined_buffer;

import std;

import vw.core;
import :frames_in_flight;
import vw.asset;
import vw.ecs;
import vw.world;
import :gpu_buffers;
import :meshing;
import :mesh_pool;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {




class vulkan_context;

struct draw_command {
    alignas(4) uint32 index_count;
    alignas(4) uint32 instance_count;
    alignas(4) uint32 first_index;
    alignas(4) int32 vertex_offset;
    alignas(4) uint32 first_instance;
};

static_assert(offsetof(draw_command, index_count) == 0);
static_assert(offsetof(draw_command, instance_count) == 4);
static_assert(offsetof(draw_command, first_index) == 8);
static_assert(offsetof(draw_command, vertex_offset) == 12);
static_assert(offsetof(draw_command, first_instance) == 16);
static_assert(sizeof(draw_command) == 20);
static_assert(sizeof(draw_command) == sizeof(vk::DrawIndexedIndirectCommand));

struct buffer_chunk_size {
    uint32 quad_count;

    bool operator==(
        const buffer_chunk_size& rhs
    ) const {
        return quad_count == rhs.quad_count;
    }

    bool operator<(
        const buffer_chunk_size& rhs
    ) const {
        return quad_count < rhs.quad_count;
    }
};

struct free_slot {
    uint32 quad_offset;
};

struct entity_allocation {
    uint32 instance_index;
    uint32 model_index;
};

struct mesh_allocation {
    uint32 quad_offset;
    uint32 quad_count;
    uint32 generation;
    uint32 ref_count;

    std::array<uint32, 6> face_counts{};
};

struct combined_buffer_stats {
    buffer_chunk_size chunk_size{};
    float32 quad_load_min    = 0.0f;
    float32 quad_load_max    = 0.0f;
    float32 quad_load_avg    = 0.0f;
    uint32 mesh_capacity     = 0;
    uint32 mesh_count        = 0;
    uint32 mesh_peak         = 0;
    uint32 mesh_high_water   = 0;
    uint32 instance_capacity = 0;
    uint32 instance_count    = 0;
};

class combined_buffer {
public:
    static constexpr uint32 cull_pass_count = 6;

    static constexpr uint32 faces_per_mesh = 6;

    explicit combined_buffer(
        vulkan_context& context,
        const buffer_chunk_size& chunk_size,
        vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout descriptor_set_layout,
        vk::DescriptorSetLayout compute_descriptor_set_layout,
        staging_buffer& staging,
        deletion_queue& deletion
    );
    ~combined_buffer();

    combined_buffer(const combined_buffer&)            = delete;
    combined_buffer& operator=(const combined_buffer&) = delete;

    combined_buffer(combined_buffer&&) noexcept            = default;
    combined_buffer& operator=(combined_buffer&&) noexcept = default;

    auto allocate(
        entity e, vw::asset::model_identity model_id, const mesh& mesh_data,
        const mat4f& transform_matrix, const vw::spatial::aabb& bounds
    ) -> void;
    auto allocate_mesh(vw::asset::model_identity model_id, const mesh& mesh_data) -> void;
    auto write_mesh(vw::asset::model_identity model_id, const mesh& mesh_data) -> void;
    auto write_transform(entity ent, const mat4f& transform_matrix, const vw::spatial::aabb& bounds) -> void;
    auto free(entity ent) -> std::optional<entity>;

    auto write_visibility(std::span<const uint32> flags) -> void;

    [[nodiscard]] auto get_entity_allocation(entity ent) -> const entity_allocation&;
    [[nodiscard]] auto get_quad_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_instance_index_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_indirect_draw_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_aabb_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_model_matrix_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_normal_matrix_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_instance_count() const -> uint32;
    [[nodiscard]] auto get_draw_command_count() const -> uint32;
    [[nodiscard]] auto get_culled_indirect_buffer() const -> vk::Buffer;
    [[nodiscard]] auto get_count_buffer() const -> vk::Buffer;
    [[nodiscard]] auto is_empty() const -> bool;
    [[nodiscard]] auto get_stats() const -> const combined_buffer_stats&;
    [[nodiscard]] auto get_descriptor_set(uint32 frame) -> vk::DescriptorSet;
    [[nodiscard]] auto get_compute_descriptor_set(uint32 frame) -> vk::DescriptorSet;

private:
    auto write_draw_command_(uint32 instance_index, const mesh_allocation& mesh_alloc) -> void;
    auto write_bounds_(
        uint32 instance_index, const mat4f& transform_matrix, const vw::spatial::aabb& bounds
    ) -> void;
    auto expand_mesh_buffers_() -> void;
    auto expand_instance_buffers_() -> void;
    auto update_descriptor_set_(uint32 frame) -> void;
    auto update_compute_descriptor_set_(uint32 frame) -> void;
    auto invalidate_descriptor_sets_() -> void;

    static constexpr uint32 default_mesh_capacity_     = 32;

    // см. docs/optimization-plan.md#42-vram-перелёт-роста-буферов
    static constexpr std::size_t growth_cap_bytes_ = 16ULL * 1024 * 1024;
    static constexpr uint32 default_instance_capacity_ = 64;

    vulkan_context* context_;
    staging_buffer* staging_;
    deletion_queue* deletion_;
    buffer_chunk_size chunk_size_;

    uint32 mesh_capacity_{default_mesh_capacity_};
    std::unique_ptr<device_storage_buffer> quad_buffer_;
    std::unique_ptr<device_storage_buffer> instance_index_buffer_;

    uint32 instance_capacity_{default_instance_capacity_};
    std::unique_ptr<device_storage_buffer> model_matrix_buffer_;
    std::unique_ptr<device_storage_buffer> normal_matrix_buffer_;
    std::unique_ptr<device_storage_buffer> indirect_draw_buffer_;
    std::unique_ptr<device_storage_buffer> aabb_buffer_;
    std::unique_ptr<device_storage_buffer> culled_indirect_buffer_;
    std::unique_ptr<device_storage_buffer> count_buffer_;
    std::unique_ptr<device_storage_buffer> visibility_buffer_;

    std::unordered_map<entity, entity_allocation> entity_allocations_;
    std::unordered_map<uint32, mesh_allocation> mesh_allocations_;
    std::unordered_map<uint32, entity> instance_indexes_;
    std::vector<free_slot> free_slots_;
    uint32 quad_used_{0};
    uint32 mesh_peak_{0};

    std::array<vk::DescriptorSet, frames_in_flight> descriptor_sets_{};
    std::array<vk::DescriptorSet, frames_in_flight> compute_descriptor_sets_{};
    uint32 stale_frames_                                    = 0;
    uint32 stale_compute_frames_                            = 0;
    vk::DescriptorPool descriptor_pool_                     = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_          = nullptr;
    vk::DescriptorSetLayout compute_descriptor_set_layout_  = nullptr;

    mutable combined_buffer_stats stats_;
};

}  // namespace vw::gfx
