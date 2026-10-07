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
    static constexpr per_face<int32> tangent_u_axis{2, 2, 0, 0, 0, 0};
    static constexpr per_face<int32> tangent_v_axis{1, 1, 2, 2, 1, 1};

    static constexpr uint32 sway_flag = 1U << 22U;

    uint32 data0 = 0;
    uint32 data1 = 0;
    uint32 data2 = 0;

    quad() = default;

    // см. docs/rendering.md#качание-листвы
    [[nodiscard]] static auto pack(
        vec3i min_pos, vec3i max_pos, face_direction face, voxel v, uint8 corners_ao,
        uint8 corners_shape, uint16 corners_sky, uint16 corners_block, bool sways
    ) -> quad;

    [[nodiscard]] static auto get_binding_descriptions()
        -> std::vector<vk::VertexInputBindingDescription>;

    [[nodiscard]] static auto get_attribute_descriptions()
        -> std::vector<vk::VertexInputAttributeDescription>;
};

// см. docs/lod-plan.md#одна-модель-на-двух-расстояниях
using vw::asset::lod_level_count;
using vw::asset::lod_level_of;
using vw::asset::lod_step_of;

struct mesh_key {
    uint32 model_index = 0;
    uint32 level       = 0;

    [[nodiscard]] auto operator==(const mesh_key&) const -> bool = default;
};

[[nodiscard]] constexpr auto mesh_key_of(vw::asset::model_identity id, int32 lod_step)
    -> mesh_key {
    return mesh_key{id.index, static_cast<uint32>(lod_level_of(lod_step))};
}

[[nodiscard]] auto effective_lod_step(const vw::asset::model& voxels, int32 requested) -> int32;

[[nodiscard]] auto entity_lod_step(const vw::ecs::model_component& comp) -> int32;

struct mesh {
    std::vector<quad> quads;

    std::array<uint32, 6> face_counts{};

    vw::asset::chunk_links links;

    int32 lod_step = 1;
};

struct mesh_options {
    bool build_links = false;

    int32 lod_step = 1;
};

// см. docs/lod-plan.md#шаг-в-мешере
struct mesh_source {
    const vw::asset::model& voxels;
    const vw::asset::model_boundary* boundary = nullptr;
    const vw::asset::light_field* sky         = nullptr;
    const vw::asset::light_field* block       = nullptr;

    int32 lod_step                                 = 1;
    const vw::asset::chunk_occupancy* lod_cells    = nullptr;
    const voxel* lod_indices                 = nullptr;

    const vw::asset::model_boundary* boundary_touched = nullptr;

    const vw::asset::chunk_occupancy* solid  = nullptr;
    const vw::asset::chunk_occupancy* leaves = nullptr;

    // см. docs/lod-plan.md#свет-сворачивается-тем-же-правилом-что-занятость
    const uint8* lod_sky   = nullptr;
    const uint8* lod_block = nullptr;

    [[nodiscard]] auto has_boundary_slice(face_direction face) const -> bool {
        return boundary != nullptr && (boundary->valid & face_bit(face)) != 0;
    }

    // см. docs/lod-plan.md#тот-же-срез-отвечает-на-два-разных-вопроса
    [[nodiscard]] auto covers_boundary_cell(face_direction face, int32 x, int32 y, int32 z) const
        -> bool {
        const vec2i on_plane = project_onto_face_plane(face, vec3i{x, y, z});
        return boundary->faces[face].test(on_plane.x, on_plane.y);
    }

    [[nodiscard]] auto touches_boundary_cell(face_direction face, int32 x, int32 y, int32 z) const
        -> bool {
        const vec2i on_plane = project_onto_face_plane(face, vec3i{x, y, z});
        const auto* plane    = boundary_touched != nullptr ? boundary_touched : boundary;
        return plane->faces[face].test(on_plane.x, on_plane.y);
    }

    [[nodiscard]] auto boundary_face(face_direction face) const
        -> const vw::asset::face_occupancy& {
        return boundary->faces[face];
    }

    // см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
    [[nodiscard]] auto shell() const -> const vw::asset::model_boundary& {
        return *(boundary_touched != nullptr ? boundary_touched : boundary);
    }

    [[nodiscard]] auto has_boundary_edge(vec3i step) const -> bool {
        return boundary != nullptr && shell().has_edge(step);
    }

    [[nodiscard]] auto touches_boundary_edge(vec3i step, int32 along) const -> bool {
        return shell().edge_holds(step, along);
    }

    [[nodiscard]] auto has_boundary_corner(vec3i step) const -> bool {
        return boundary != nullptr && shell().has_corner(step);
    }

    [[nodiscard]] auto touches_boundary_corner(vec3i step) const -> bool {
        return shell().corner_holds(step);
    }

    [[nodiscard]] auto sky_light() const -> const vw::asset::light_field* {
        return sky;
    }

    [[nodiscard]] auto block_light() const -> const vw::asset::light_field* {
        return block;
    }

    [[nodiscard]] auto cells_x() const -> int32 {
        return voxels.width() / lod_step;
    }

    [[nodiscard]] auto cells_y() const -> int32 {
        return voxels.height() / lod_step;
    }

    [[nodiscard]] auto cells_z() const -> int32 {
        return voxels.depth() / lod_step;
    }

    [[nodiscard]] auto cell_empty(int32 x, int32 y, int32 z) const -> bool {
        if (lod_step == 1) {
            return voxels.is_empty(x, y, z);
        }
        return !lod_cells->test(x, y, z);
    }

    [[nodiscard]] auto cell_offset(int32 x, int32 y, int32 z) const -> int32 {
        return (((z * cells_y()) + y) * cells_x()) + x;
    }

    [[nodiscard]] auto cell_inside(vec3i cell) const -> bool {
        return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 && cell.x < cells_x() &&
               cell.y < cells_y() && cell.z < cells_z();
    }

    [[nodiscard]] auto cell_index(int32 x, int32 y, int32 z) const -> voxel {
        if (lod_step == 1) {
            return voxels.get_voxel(x, y, z);
        }
        return lod_indices[cell_offset(x, y, z)];
    }
};

class simple_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_source src,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto add_cube_face(
        std::vector<quad>& quads,
        mesh_source src,
        int32 x,
        int32 y,
        int32 z,
        face_direction face,
        voxel voxel_id,
        mesh_options opts
    ) -> void;

    [[nodiscard]]
    static auto is_face_visible(
        mesh_source src, int32 x, int32 y, int32 z,
        face_direction face
    ) -> bool;
};

struct corner_light {
    uint16 sky   = 0;
    uint16 block = 0;

    [[nodiscard]] auto operator==(const corner_light&) const -> bool = default;
};

struct face_mask_cell {
    voxel index;
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

    std::unique_ptr<vw::asset::chunk_occupancy> lod_cells;
    std::unique_ptr<vw::asset::chunk_occupancy> leaves;
    std::vector<voxel> lod_indices;
    vw::asset::model_boundary lod_boundary;
    vw::asset::model_boundary lod_boundary_touched;
    std::vector<uint8> lod_sky;
    std::vector<uint8> lod_block;

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
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto merge_and_emit_strips(
        mesh_generation_storage& storage,
        const detail::face_axis_mapping& axes,
        face_direction face,
        int32 layer,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        face_direction face,
        mesh_options opts
    ) -> void;
};

class greedy_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_generation_storage& storage,
        mesh_source src,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto merge_and_emit_rects_bits(
        mesh_generation_storage& storage,
        const detail::face_axis_mapping& axes,
        face_direction face,
        int32 layer,
        detail::layer_rows& rows
    ) -> void;

    static auto merge_and_emit_rects(
        mesh_generation_storage& storage,
        const detail::face_axis_mapping& axes,
        face_direction face,
        int32 layer,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        face_direction face,
        mesh_options opts
    ) -> void;
};
}  // namespace vw::gfx

export template <>
struct std::hash<vw::gfx::mesh_key> {
    auto operator()(const vw::gfx::mesh_key& key) const noexcept -> std::size_t {
        std::size_t x = (std::size_t{key.level} << 32) | std::size_t{key.model_index};

        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = x ^ (x >> 31);

        return x;
    }
};
