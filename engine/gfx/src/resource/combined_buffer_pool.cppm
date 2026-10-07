export module vw.gfx:resource.combined_buffer_pool;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :camera;
import :gpu_buffers;
import :meshing;
import :mesh_pool;
import :resource.combined_buffer;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {


class vulkan_context;

struct entity_buffer_info {
    buffer_chunk_size chunk_size;
    std::size_t buffer_index;
    vw::spatial::aabb bounds{};
    world_light light{};
    bool lit_by_world = false;
    instance_corners corners{};
    uint32 volume_model = asset::model_identity::invalid_index;
};

struct buffer_pool_timing_stats {
    float32 destroyed_ms     = 0.0f;
    float32 meshes_ms        = 0.0f;
    float32 transforms_ms    = 0.0f;
    float32 light_ms         = 0.0f;
    float32 staging_flush_ms = 0.0f;
};

struct chunk_cull_stats {
    uint32 chunks  = 0;
    uint32 visible = 0;
    float32 walk_ms = 0.0f;

    uint32 visited       = 0;
    uint32 visited_empty = 0;
    uint32 sealed        = 0;
    uint32 known_links   = 0;
    uint32 merged        = 0;
    uint32 max_pockets   = 0;
};

struct combined_buffer_pool_stats {
    float32 quad_load_min    = 0.0f;
    float32 quad_load_max    = 0.0f;
    float32 quad_load_avg    = 0.0f;
    uint32 mesh_capacity     = 0;
    uint32 mesh_count        = 0;
    uint32 instance_capacity = 0;
    uint32 instance_count    = 0;

    uint32 mesh_pending     = 0;
    uint32 transform_pending = 0;

    std::vector<combined_buffer_stats> buffers;
    buffer_pool_timing_stats timing;
    chunk_cull_stats chunk_cull;
};

class combined_buffer_pool {
public:
    using world_type = world;

    explicit combined_buffer_pool(
        vulkan_context& context,
        deletion_queue& deletion,
        vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout descriptor_set_layout,
        vk::DescriptorSetLayout compute_descriptor_set_layout,
        model_occupancy_buffer& model_volumes
    );
    ~combined_buffer_pool() = default;

    combined_buffer_pool(const combined_buffer_pool&)            = delete;
    auto operator=(const combined_buffer_pool&) -> combined_buffer_pool& = delete;
    combined_buffer_pool(combined_buffer_pool&&)                 = delete;
    auto operator=(combined_buffer_pool&&) -> combined_buffer_pool&      = delete;

    auto track(world_type& world) -> void;

    auto mark_mesh_ready(entity ent) -> void;

    auto update(
        world_type& world,
        const camera& camera,
        vk::CommandBuffer cmd,
        mesh_pool& pool
    ) -> void;

    [[nodiscard]] auto get_buffers() const -> const std::vector<std::unique_ptr<combined_buffer>>&;

    [[nodiscard]] auto get_index_buffer() const -> vk::Buffer;

    [[nodiscard]] static auto get_chunk_size_for_mesh(
        uint32 quad_count
    ) -> buffer_chunk_size;

    [[nodiscard]] auto get_stats() const -> const combined_buffer_pool_stats&;

    auto set_chunk_cull_enabled(bool enabled) -> void {
        chunk_cull_enabled_ = enabled;
    }

    [[nodiscard]] auto is_chunk_cull_enabled() const -> bool {
        return chunk_cull_enabled_;
    }

    [[nodiscard]] auto get_touched_bounds() const -> std::span<const vw::spatial::aabb> {
        return touched_bounds_;
    }

    [[nodiscard]] auto mesh_write_budget() const -> uint32;

private:
    auto get_or_create_buffer(const buffer_chunk_size& chunk_size) -> combined_buffer*;

    auto process_destroyed_(world_type& world) -> void;
    auto update_meshes_(world_type& world, const vec3f& camera_pos, mesh_pool& pool) -> void;
    auto update_transforms_(world_type& world) -> void;
    auto update_instance_light_(world_type& world) -> void;
    auto rewrite_swapped_(world_type& world, std::size_t buffer_index, entity swapped) -> void;
    auto release_volume_(entity_buffer_info& info) -> void;
    auto update_chunk_visibility_(
        world_type& world, const vec3f& camera_pos, bool sight_starts_at_a_point
    ) -> void;
    auto hide_marked_() -> void;
    auto evict_uploaded_(world_type& world, mesh_pool& pool) -> void;

    vulkan_context* context_;
    model_occupancy_buffer* model_volumes_;
    deletion_queue* deletion_;
    staging_buffer staging_;
    auto ensure_index_pattern_(uint32 quads) -> void;

    std::vector<std::unique_ptr<combined_buffer>> buffers_;
    std::unique_ptr<device_index_buffer> index_buffer_;
    std::unique_ptr<index_buffer> index_upload_;
    uint32 index_quads_ = 0;
    ecs::component_pool<entity_buffer_info> entity_buffer_infos_;
    std::map<buffer_chunk_size, std::size_t> chunk_size_to_buffer_index_;

    vk::DescriptorPool descriptor_pool_                     = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_          = nullptr;
    vk::DescriptorSetLayout compute_descriptor_set_layout_  = nullptr;

    std::vector<entity> entities_to_process_;
    std::vector<entity> destroyed_pending_entities_;
    std::vector<entity> mesh_pending_entities_;
    std::vector<entity> transform_pending_entities_;
    std::vector<entity> merge_buffer_;
    std::vector<vw::spatial::aabb> touched_bounds_;
    std::vector<std::pair<float32, entity>> sort_keys_;

    std::vector<std::pair<vw::asset::model_identity, int32>> uploaded_models_;
    std::unordered_set<vw::asset::model_identity> awaited_models_;

    bool chunk_cull_enabled_ = false;
    std::vector<std::vector<uint32>> visibility_flags_;

    std::unordered_set<entity> hidden_entities_;

    std::unordered_map<entity, vw::asset::chunk_links> chunk_links_;

    std::unordered_map<vec2i, int32> column_top_;
    ecs::chunk_walk_scratch chunk_walk_scratch_;

    mutable combined_buffer_pool_stats stats_;
};

}  // namespace vw::gfx
