export module vw.gfx:meshing;

import std;

import vw.core;
import vw.asset;
import vw.world;
import vulkan;

namespace vw::gfx::detail {
struct face_axis_mapping;
struct layer_rows;
}  // namespace vw::gfx::detail

export namespace vw::gfx {

struct quad {
    uint32 data0 = 0;
    uint32 data1 = 0;
    uint32 data2 = 0;

    quad() = default;

    [[nodiscard]] static auto pack(
        vec3i min_pos, vec3i max_pos, uint8 normal_id, voxel_slot slot, uint8 corners_ao,
        uint8 corners_convex, uint16 corners_sky, uint16 corners_block
    ) -> quad;

    [[nodiscard]] static auto get_binding_descriptions()
        -> std::vector<vk::VertexInputBindingDescription>;

    [[nodiscard]] static auto get_attribute_descriptions()
        -> std::vector<vk::VertexInputAttributeDescription>;
};

struct mesh {
    std::vector<quad> quads;

    std::array<uint32, 6> face_counts{};

    vw::asset::chunk_links links;

    auto release_data() -> void {
        quads = {};
    }
};

struct mesh_options {
    bool build_links = false;
};

struct mesh_source {
    const vw::asset::model& voxels;
    const vw::asset::chunk_volume* chunk = nullptr;

    [[nodiscard]] auto has_boundary_slice(int32 face_direction) const -> bool {
        return chunk != nullptr && chunk->has_boundary_slice(face_direction);
    }

    [[nodiscard]] auto is_boundary_solid(int32 face_direction, int32 x, int32 y, int32 z) const
        -> bool {
        return chunk->is_boundary_solid(face_direction, x, y, z);
    }

    [[nodiscard]] auto boundary_face(int32 face_direction) const
        -> const vw::asset::face_occupancy& {
        return chunk->get_boundary_face(face_direction);
    }

    [[nodiscard]] auto sky_light() const -> const vw::asset::light_field* {
        return chunk != nullptr ? chunk->get_sky_light() : nullptr;
    }

    [[nodiscard]] auto block_light() const -> const vw::asset::light_field* {
        return chunk != nullptr ? chunk->get_block_light() : nullptr;
    }
};

class simple_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto add_cube_face(
        std::vector<quad>& quads,
        mesh_source src,
        int32 x,
        int32 y,
        int32 z,
        int32 face_direction,
        voxel voxel_id,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    [[nodiscard]]
    static auto is_face_visible(
        mesh_source src, int32 x, int32 y, int32 z,
        int32 face_direction
    ) -> bool;
};

struct corner_light {
    uint16 sky   = 0;
    uint16 block = 0;

    [[nodiscard]] auto operator==(const corner_light&) const -> bool = default;
};

struct face_mask_cell {
    voxel_index index;
    uint8 corner_ao;

    corner_light light{};

    uint8 corner_convex = 0;

    [[nodiscard]]
    auto operator==(const face_mask_cell&) const -> bool = default;

    [[nodiscard]]
    auto is_empty() const -> bool {
        return index.is_empty();
    }
};

struct mesh_generation_storage {
    std::vector<quad> quads;
    std::vector<face_mask_cell> mask;
    std::vector<bool> depth_has_pages;

    std::unique_ptr<vw::asset::chunk_occupancy> occupancy;
    bool occupancy_valid = false;

    vw::asset::chunk_link_scratch link_scratch;

    auto clear() -> void {
        quads.clear();
    }
};

class strip_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_generation_storage& storage,
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto merge_and_emit_strips(
        mesh_generation_storage& storage,
        mesh_source src,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        int32 face_direction,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;
};

class greedy_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_generation_storage& storage,
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto merge_and_emit_rects_bits(
        mesh_generation_storage& storage,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        detail::layer_rows& rows,
        const std::array<uint16, 256>& slots
    ) -> void;

    static auto merge_and_emit_rects(
        mesh_generation_storage& storage,
        mesh_source src,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        int32 face_direction,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;
};
}  // namespace vw::gfx
