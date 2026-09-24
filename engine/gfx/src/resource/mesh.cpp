module;

#include <cstddef>

module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

auto quad::pack(
    vec3i min_pos,
    vec3i max_pos,
    face_direction face,
    voxel v,
    uint8 corners_ao,
    uint8 corners_convex,
    uint16 corners_sky,
    uint16 corners_block
) -> quad {
    const int32 u_axis = tangent_u_axis[face];
    const int32 v_axis = tangent_v_axis[face];

    const auto span_u = static_cast<uint32>(max_pos[u_axis] - min_pos[u_axis] - 1);
    const auto span_v = static_cast<uint32>(max_pos[v_axis] - min_pos[v_axis] - 1);

    quad q;
    q.data0 =                                               //
        (static_cast<uint32>(min_pos.x) & 0x7Fu) |          //
        ((static_cast<uint32>(min_pos.y) & 0x7Fu) << 7) |   //
        ((static_cast<uint32>(min_pos.z) & 0x7Fu) << 14) |  //
        ((static_cast<uint32>(face) & 0x7u) << 21) |        //
        (static_cast<uint32>(corners_ao) << 24);

    q.data1 =                                               //
        (span_u & 0x7Fu) |                                  //
        ((span_v & 0x7Fu) << 7) |                           //
        (static_cast<uint32>(v.value) << 14) |               //
        (static_cast<uint32>(corners_convex) << 24);

    q.data2 = static_cast<uint32>(corners_sky) | (static_cast<uint32>(corners_block) << 16);

    return q;
}

auto effective_lod_step(
    const vw::asset::model& voxels, int32 requested
) -> int32 {
    constexpr int32 side = vw::asset::chunk_occupancy::side;

    const auto step = static_cast<int32>(std::bit_floor(
        static_cast<uint32>(std::clamp(requested, 1, lod_step_of(lod_level_count - 1)))
    ));

    if (step == 1) {
        return 1;
    }
    if (voxels.width() != side || voxels.height() != side || voxels.depth() != side) {
        return 1;
    }
    return step;
}

auto entity_lod_step(
    const vw::ecs::model_component& comp
) -> int32 {
    if (!comp.has_model()) {
        return 1;
    }
    return effective_lod_step(
        *comp.get_model(), lod_step_of(static_cast<int32>(comp.get_lod_level()))
    );
}

auto quad::get_binding_descriptions() -> std::vector<vk::VertexInputBindingDescription> {
    std::vector binding_descriptions = {
        vk::VertexInputBindingDescription{
            .binding   = 0,
            .stride    = sizeof(uint32),
            .inputRate = vk::VertexInputRate::eInstance,
        },
    };
    return binding_descriptions;
}

auto quad::get_attribute_descriptions() -> std::vector<vk::VertexInputAttributeDescription> {
    std::vector attribute_descriptions = {
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = vk::Format::eR32Uint,
            .offset   = 0,
        },
    };
    return attribute_descriptions;
}


static constexpr per_face<vec3i> ao_tangent_u = {
    vec3i{0, 0, 1},
    vec3i{0, 0, 1},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
};

static constexpr per_face<vec3i> ao_tangent_v = {
    vec3i{0, 1, 0},
    vec3i{0, 1, 0},
    vec3i{0, 0, 1},
    vec3i{0, 0, 1},
    vec3i{0, 1, 0},
    vec3i{0, 1, 0},
};


namespace detail {

struct face_axis_mapping {
    int32 width, height, depth;
    face_direction face;
    int32 world_units_per_voxel;
    int32 step;

    face_axis_mapping(mesh_source src, face_direction direction);

    [[nodiscard]] auto to_model_coords(int32 u, int32 v, int32 layer) const
        -> std::tuple<int32, int32, int32>;

    [[nodiscard]] auto to_local_min_max(int32 u, int32 v, int32 w, int32 h, int32 layer) const
        -> std::pair<vec3i, vec3i>;
};

[[nodiscard]] auto compute_corner_darkness(mesh_source src, int32 x, int32 y, int32 z,
                                           face_direction face) -> uint8;
[[nodiscard]] auto compute_corner_convexity(mesh_source src, int32 x, int32 y, int32 z,
                                            face_direction face) -> uint8;
[[nodiscard]] auto compute_corner_light(mesh_source src, int32 x, int32 y, int32 z,
                                        face_direction face) -> corner_light;

inline constexpr face_direction convex_face = face_direction::pos_y;

[[nodiscard]] auto is_face_visible(mesh_source src, int32 x, int32 y, int32 z,
                                   face_direction face) -> bool;

auto build_face_mask(
    mesh_generation_storage& storage,
    mesh_source src,
    const face_axis_mapping& axes,
    face_direction face,
    int32 layer,
    mesh_options opts
) -> void;

auto add_quad(
    std::vector<quad>& quads,
    face_direction face,
    vec3i min_pos,
    vec3i max_pos,
    voxel v,
    uint8 corner_ao,
    uint8 corner_convex,
    corner_light light
) -> void;

struct layer_rows {
    std::array<uint64, 64> visible{};
    std::array<uint64, 64> front{};

    std::array<uint64, 64> own{};

    bool front_outside = false;
};

[[nodiscard]] auto light_from_rows(mesh_source src, const layer_rows& rows,
                                   int32 u_at, int32 v_at, int32 x, int32 y, int32 z,
                                   face_direction face) -> corner_light;

[[nodiscard]] auto build_layer_rows(
    mesh_source src,
    const vw::asset::chunk_occupancy& occupancy,
    const face_axis_mapping& axes,
    face_direction face,
    int32 layer,
    layer_rows& out
) -> bool;

auto emit_rect(
    mesh_generation_storage& storage,
    const face_axis_mapping& axes,
    face_direction face,
    int32 layer,
    int32 u_start,
    int32 v_start,
    int32 w,
    int32 h,
    const face_mask_cell& cell
) -> void;

face_axis_mapping::face_axis_mapping(
    mesh_source src, face_direction direction
)
    : face(direction),
      world_units_per_voxel(src.voxels.world_units_per_voxel()),
      step(src.lod_step) {
    switch (axis_of(direction)) {
        case 0:
            width  = src.cells_z();
            height = src.cells_y();
            depth  = src.cells_x();
            break;
        case 1:
            width  = src.cells_x();
            height = src.cells_z();
            depth  = src.cells_y();
            break;
        default:
            width  = src.cells_x();
            height = src.cells_y();
            depth  = src.cells_z();
            break;
    }
}

[[nodiscard]] auto face_axis_mapping::to_model_coords(
    int u, int v, int layer
) const -> std::tuple<int, int, int> {
    int d = is_positive(face) ? layer : depth - 1 - layer;
    switch (axis_of(face)) {
        case 0:
            return {d, v, u};
        case 1:
            return {u, d, v};
        default:
            return {u, v, d};
    }
}

[[nodiscard]] auto face_axis_mapping::to_local_min_max(
    int32 u, int32 v, int32 w, int32 h, int32 layer
) const -> std::pair<vec3i, vec3i> {
    const int32 d_cell = is_positive(face) ? layer : depth - 1 - layer;

    // см. docs/lod-plan.md#плоскость-положительной-грани
    const int32 d_lo = is_positive(face) ? ((d_cell + 1) * step) - 1 : d_cell * step;
    const int32 d_hi = d_lo + 1;

    const int32 u_lo = u * step;
    const int32 v_lo = v * step;
    const int32 u_hi = (u + w) * step;
    const int32 v_hi = (v + h) * step;

    switch (axis_of(face)) {
        case 0:
            return {{d_lo, v_lo, u_lo}, {d_hi, v_hi, u_hi}};
        case 1:
            return {{u_lo, d_lo, v_lo}, {u_hi, d_hi, v_hi}};
        default:
            return {{u_lo, v_lo, d_lo}, {u_hi, v_hi, d_hi}};
    }
}

auto is_solid_at(
    mesh_source src, vec3i p
) -> bool {
    const bool ox = p.x < 0 || p.x >= src.cells_x();
    const bool oy = p.y < 0 || p.y >= src.cells_y();
    const bool oz = p.z < 0 || p.z >= src.cells_z();

    if (!ox && !oy && !oz) {
        return !src.cell_empty(p.x, p.y, p.z);
    }
    if (static_cast<int32>(ox) + static_cast<int32>(oy) + static_cast<int32>(oz) > 1) {
        return false;
    }

    using enum face_direction;

    if (p.x >= src.cells_x() && src.has_boundary_slice(pos_x)) {
        return src.is_boundary_solid(pos_x, 0, p.y, p.z);
    }
    if (p.x < 0 && src.has_boundary_slice(neg_x)) {
        return src.is_boundary_solid(neg_x, 0, p.y, p.z);
    }
    if (p.y >= src.cells_y() && src.has_boundary_slice(pos_y)) {
        return src.is_boundary_solid(pos_y, p.x, 0, p.z);
    }
    if (p.y < 0 && src.has_boundary_slice(neg_y)) {
        return src.is_boundary_solid(neg_y, p.x, 0, p.z);
    }
    if (p.z >= src.cells_z() && src.has_boundary_slice(pos_z)) {
        return src.is_boundary_solid(pos_z, p.x, p.y, 0);
    }
    if (p.z < 0 && src.has_boundary_slice(neg_z)) {
        return src.is_boundary_solid(neg_z, p.x, p.y, 0);
    }

    return false;
}

[[nodiscard]] auto corner_level(bool edge_a, bool edge_b, bool diagonal) -> uint8 {
    if (edge_a && edge_b) {
        return 3;
    }
    return static_cast<uint8>(edge_a) + static_cast<uint8>(edge_b) +
           static_cast<uint8>(diagonal);
}

[[nodiscard]] auto is_open_at(mesh_source src, vec3i p) -> bool {
    const bool ox = p.x < 0 || p.x >= src.cells_x();
    const bool oy = p.y < 0 || p.y >= src.cells_y();
    const bool oz = p.z < 0 || p.z >= src.cells_z();

    if (!ox && !oy && !oz) {
        return src.cell_empty(p.x, p.y, p.z);
    }
    if (static_cast<int32>(ox) + static_cast<int32>(oy) + static_cast<int32>(oz) > 1) {
        return false;
    }

    using enum face_direction;

    if (p.x >= src.cells_x()) {
        return src.has_boundary_slice(pos_x) && !src.is_boundary_solid(pos_x, 0, p.y, p.z);
    }
    if (p.x < 0) {
        return src.has_boundary_slice(neg_x) && !src.is_boundary_solid(neg_x, 0, p.y, p.z);
    }
    if (p.y >= src.cells_y()) {
        return src.has_boundary_slice(pos_y) && !src.is_boundary_solid(pos_y, p.x, 0, p.z);
    }
    if (p.y < 0) {
        return src.has_boundary_slice(neg_y) && !src.is_boundary_solid(neg_y, p.x, 0, p.z);
    }
    if (p.z >= src.cells_z()) {
        return src.has_boundary_slice(pos_z) && !src.is_boundary_solid(pos_z, p.x, p.y, 0);
    }
    return src.has_boundary_slice(neg_z) && !src.is_boundary_solid(neg_z, p.x, p.y, 0);
}

[[nodiscard]] auto corner_open_level(bool open_a, bool open_b, bool open_diagonal) -> uint8 {
    if (open_a && open_b) {
        return 3;
    }
    return static_cast<uint8>(open_a) + static_cast<uint8>(open_b) +
           static_cast<uint8>(open_diagonal);
}

[[nodiscard]] auto compute_corner_convexity(
    mesh_source src, int x, int y, int z, face_direction face
) -> uint8 {
    if (face != convex_face) {
        return 0;
    }

    const vec3i host = vec3i{x, y, z};
    const vec3i u    = ao_tangent_u[face];
    const vec3i v    = ao_tangent_v[face];

    const bool open_mu = is_open_at(src, host - u);
    const bool open_pu = is_open_at(src, host + u);
    const bool open_mv = is_open_at(src, host - v);
    const bool open_pv = is_open_at(src, host + v);

    const bool diag_c0 = is_open_at(src, host - u - v);
    const bool diag_c1 = is_open_at(src, host + u - v);
    const bool diag_c2 = is_open_at(src, host + u + v);
    const bool diag_c3 = is_open_at(src, host - u + v);

    const uint8 c0 = corner_open_level(open_mu, open_mv, diag_c0);
    const uint8 c1 = corner_open_level(open_pu, open_mv, diag_c1);
    const uint8 c2 = corner_open_level(open_pu, open_pv, diag_c2);
    const uint8 c3 = corner_open_level(open_mu, open_pv, diag_c3);

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

auto compute_corner_darkness(
    mesh_source src, int x, int y, int z, face_direction face
) -> uint8 {
    const vec3i n = vec3i{x, y, z} + offset_of(face);
    const vec3i u = ao_tangent_u[face];
    const vec3i v = ao_tangent_v[face];

    const bool edge_mu = is_solid_at(src, n - u);
    const bool edge_pu = is_solid_at(src, n + u);
    const bool edge_mv = is_solid_at(src, n - v);
    const bool edge_pv = is_solid_at(src, n + v);

    const bool diag_c0 = is_solid_at(src, n - u - v);
    const bool diag_c1 = is_solid_at(src, n + u - v);
    const bool diag_c2 = is_solid_at(src, n + u + v);
    const bool diag_c3 = is_solid_at(src, n - u + v);

    const uint8 c0 = corner_level(edge_mu, edge_mv, diag_c0);
    const uint8 c1 = corner_level(edge_pu, edge_mv, diag_c1);
    const uint8 c2 = corner_level(edge_pu, edge_pv, diag_c2);
    const uint8 c3 = corner_level(edge_mu, edge_pv, diag_c3);

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}


[[nodiscard]] auto average_of(int32 sum, int32 count) -> int32 {
    switch (count) {
        case 1:
            return sum;
        case 2:
            return sum >> 1;
        case 4:
            return sum >> 2;
        default:
            return (sum * 21846) >> 16;
    }
}

[[nodiscard]] constexpr auto patch_slot(int32 du, int32 dv) -> std::size_t {
    return static_cast<std::size_t>(((dv + 1) * 3) + (du + 1));
}

[[nodiscard]] constexpr auto patch_bit(int32 du, int32 dv) -> uint32 {
    return 1U << patch_slot(du, dv);
}

constexpr std::size_t front_cell_slot = patch_slot(0, 0);
constexpr uint32 front_cell_bit       = patch_bit(0, 0);

auto corners_from_patch(
    mesh_source src, vec3i n, vec3i u, vec3i v, uint32 open_patch
) -> corner_light {
    const auto* sky   = src.sky_light();
    const auto* block = src.block_light();

    const auto flat = [](const vw::asset::light_field* field, uint16 absent) -> uint16 {
        if (field == nullptr) {
            return absent;
        }
        return static_cast<uint16>(static_cast<uint16>(field->uniform_level()) * 0x1111U);
    };

    const bool walk_sky   = sky != nullptr && !sky->is_uniform();
    const bool walk_block = block != nullptr && !block->is_uniform();

    if (!walk_sky && !walk_block) {
        return corner_light{.sky = flat(sky, 0xFFFFU), .block = flat(block, 0x0000U)};
    }

    std::array<int32, 9> lit_sky{};
    std::array<int32, 9> lit_block{};
    std::array<int32, 9> open{};

    vec3i row = n - u - v;

    for (int32 dv = -1; dv <= 1; ++dv) {
        vec3i cell = row;

        for (int32 du = -1; du <= 1; ++du) {
            const auto slot = patch_slot(du, dv);

            open[slot] = (open_patch & patch_bit(du, dv)) != 0 ? 1 : 0;
            if (open[slot] != 0) {
                const vec3i at = src.cell_center_voxel(cell);
                if (walk_sky) {
                    lit_sky[slot] = sky->level_around(at.x, at.y, at.z);
                }
                if (walk_block) {
                    lit_block[slot] = block->level_around(at.x, at.y, at.z);
                }
            }

            cell = cell + u;
        }

        row = row + v;
    }

    const auto pack_corners = [&](const std::array<int32, 9>& lit) -> uint16 {
        const auto corner = [&](int32 du, int32 dv) -> uint16 {
            const std::size_t along_u  = patch_slot(du, 0);
            const std::size_t along_v  = patch_slot(0, dv);
            const std::size_t diagonal = patch_slot(du, dv);

            const int32 sum   = lit[front_cell_slot] + lit[along_u] + lit[along_v] + lit[diagonal];
            const int32 count = 1 + open[along_u] + open[along_v] + open[diagonal];
            return static_cast<uint16>(average_of(sum, count));
        };

        const uint16 c0 = corner(-1, -1);
        const uint16 c1 = corner(1, -1);
        const uint16 c2 = corner(1, 1);
        const uint16 c3 = corner(-1, 1);

        return static_cast<uint16>(c0 | (c1 << 4) | (c2 << 8) | (c3 << 12));
    };

    return corner_light{
        .sky   = walk_sky ? pack_corners(lit_sky) : flat(sky, 0xFFFFU),
        .block = walk_block ? pack_corners(lit_block) : flat(block, 0x0000U),
    };
}

auto compute_corner_light(
    mesh_source src, int x, int y, int z, face_direction face
) -> corner_light {
    const vec3i n = vec3i{x, y, z} + offset_of(face);
    const vec3i u = ao_tangent_u[face];
    const vec3i v = ao_tangent_v[face];

    uint32 open_patch = front_cell_bit;

    for (int32 dv = -1; dv <= 1; ++dv) {
        for (int32 du = -1; du <= 1; ++du) {
            if (du == 0 && dv == 0) {
                continue;
            }

            vec3i at = n;
            if (du < 0) {
                at = at - u;
            } else if (du > 0) {
                at = at + u;
            }
            if (dv < 0) {
                at = at - v;
            } else if (dv > 0) {
                at = at + v;
            }

            open_patch |= is_solid_at(src, at) ? 0U : patch_bit(du, dv);
        }
    }

    return corners_from_patch(src, n, u, v, open_patch);
}

auto light_from_rows(
    mesh_source src, const layer_rows& rows, int32 u_at, int32 v_at, int x,
    int y, int z, face_direction face
) -> corner_light {
    uint32 open_patch = 0;

    for (int32 dv = -1; dv <= 1; ++dv) {
        const uint64 row = rows.front[v_at + dv];
        for (int32 du = -1; du <= 1; ++du) {
            open_patch |= ((row >> (u_at + du)) & 1U) == 0 ? patch_bit(du, dv) : 0U;
        }
    }
    open_patch |= front_cell_bit;

    return corners_from_patch(
        src, vec3i{x, y, z} + offset_of(face), ao_tangent_u[face], ao_tangent_v[face],
        open_patch
    );
}

auto is_face_visible(
    mesh_source src, int32 x, int32 y, int32 z, face_direction face
) -> bool {
    const vec3i towards = offset_of(face);

    const int32 nx = x + towards.x;
    const int32 ny = y + towards.y;
    const int32 nz = z + towards.z;

    if (nx < 0 || nx >= src.cells_x() || ny < 0 || ny >= src.cells_y() || nz < 0 ||
        nz >= src.cells_z()) {
        if (src.has_boundary_slice(face)) {
            return !src.is_boundary_solid(face, x, y, z);
        }
        return true;
    }

    return src.cell_empty(nx, ny, nz);
}

auto build_face_mask(
    mesh_generation_storage& storage,
    mesh_source src,
    const face_axis_mapping& axes,
    face_direction face,
    int layer,
    [[maybe_unused]] mesh_options opts
) -> void {
    constexpr int ps = vw::asset::model::page_size;

    auto idx = [&](int u, int v) -> std::size_t {
        return (static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height)) + static_cast<std::size_t>(v);
    };

    constexpr face_mask_cell empty_cell{voxel{}, 0};

    for (int u_block = 0; u_block < axes.width; u_block += ps) {
        int u_end = std::min(u_block + ps, axes.width);
        for (int v_block = 0; v_block < axes.height; v_block += ps) {
            int v_end = std::min(v_block + ps, axes.height);

            auto [pmx, pmy, pmz] = axes.to_model_coords(u_block, v_block, layer);
            auto pm              = src.voxels.get_page_mode(pmx / ps, pmy / ps, pmz / ps);

            if (pm == vw::asset::page_mode::empty) {
                for (int u = u_block; u < u_end; u++) {
                    for (int v = v_block; v < v_end; v++) {
                        storage.mask[idx(u, v)] = empty_cell;
                    }
                }
                continue;
            }

            if (pm == vw::asset::page_mode::uniform) {
                const auto fid = src.voxels.get_page_fill(pmx / ps, pmy / ps, pmz / ps);
                for (int u = u_block; u < u_end; u++) {
                    for (int v = v_block; v < v_end; v++) {
                        auto [mx, my, mz] = axes.to_model_coords(u, v, layer);
                        if (is_face_visible(src, mx, my, mz, face)) {
                            storage.mask[idx(u, v)] = {
                                fid,
                                compute_corner_darkness(src, mx, my, mz, face),
                                compute_corner_light(src, mx, my, mz, face),
                                compute_corner_convexity(src, mx, my, mz, face)
                            };
                        } else {
                            storage.mask[idx(u, v)] = empty_cell;
                        }
                    }
                }
                continue;
            }

            const auto page = src.voxels.get_page(pmx / ps, pmy / ps, pmz / ps);
            for (int u = u_block; u < u_end; u++) {
                for (int v = v_block; v < v_end; v++) {
                    auto [mx, my, mz] = axes.to_model_coords(u, v, layer);
                    const auto vx     = page.voxel_at(mx % ps, my % ps, mz % ps);
                    if (!vx.is_empty() && is_face_visible(src, mx, my, mz, face)) {
                        storage.mask[idx(u, v)] = {
                            vx,
                            compute_corner_darkness(src, mx, my, mz, face),
                            compute_corner_light(src, mx, my, mz, face),
                            compute_corner_convexity(src, mx, my, mz, face)
                        };
                    } else {
                        storage.mask[idx(u, v)] = empty_cell;
                    }
                }
            }
        }
    }
}

auto add_quad(
    std::vector<quad>& quads,
    face_direction face,
    vec3i min_pos,
    vec3i max_pos,
    voxel v,
    uint8 corner_ao,
    uint8 corner_convex,
    corner_light light
) -> void {
    using corner_order = std::array<uint8, 4>;

    static constexpr per_face<corner_order> winding_to_corner = {
        corner_order{0, 1, 3, 2},
        corner_order{0, 2, 3, 1},
        corner_order{0, 1, 3, 2},
        corner_order{0, 2, 3, 1},
        corner_order{0, 2, 3, 1},
        corner_order{1, 3, 2, 0},
    };

    static constexpr uint8 corner_to_ao[4] = {0, 1, 3, 2};

    const uint8 c_ao[4] = {
        static_cast<uint8>(corner_ao & 0x3u),
        static_cast<uint8>((corner_ao >> 2) & 0x3u),
        static_cast<uint8>((corner_ao >> 4) & 0x3u),
        static_cast<uint8>((corner_ao >> 6) & 0x3u),
    };

    uint8 ao_winding     = 0;
    uint8 convex_winding = 0;
    uint16 sky_winding   = 0;
    uint16 block_winding = 0;
    for (int i = 0; i < 4; i++) {
        const uint8 corner_i = winding_to_corner[face][i];
        const uint8 ao_i     = corner_to_ao[corner_i];
        ao_winding |= static_cast<uint8>(c_ao[ao_i] << (i * 2));

        // см. docs/rendering.md#порядок-углов
        const auto level = static_cast<uint16>((light.sky >> (ao_i * 4)) & 0xFu);
        sky_winding |= static_cast<uint16>(level << (i * 4));

        const auto lamp = static_cast<uint16>((light.block >> (ao_i * 4)) & 0xFu);
        block_winding |= static_cast<uint16>(lamp << (i * 4));

        const auto out = static_cast<uint8>((corner_convex >> (ao_i * 2)) & 0x3u);
        convex_winding |= static_cast<uint8>(out << (i * 2));
    }

    quads.push_back(quad::pack(
        min_pos, max_pos, face, v, ao_winding, convex_winding, sky_winding,
        block_winding
    ));
}


auto boundary_row(
    mesh_source src, face_direction face, int32 v, int32 width
) -> uint64 {
    if (!src.has_boundary_slice(face)) {
        return 0;
    }

    const auto& plane = src.boundary_face(face);

    if (axis_of(face) == 0) {
        uint64 bits = 0;
        for (int32 z = 0; z < width; ++z) {
            if (plane.test(v, z)) {
                bits |= uint64{1} << z;
            }
        }
        return bits;
    }

    return plane.rows[v];
}

auto build_layer_rows(
    mesh_source src,
    const vw::asset::chunk_occupancy& occupancy,
    const face_axis_mapping& axes,
    face_direction face,
    int32 layer,
    layer_rows& out
) -> bool {
    const int32 d     = is_positive(face) ? layer : axes.depth - 1 - layer;
    const int32 step  = is_positive(face) ? 1 : -1;
    const int32 nd    = d + step;
    const bool inside = nd >= 0 && nd < axes.depth;

    out.front_outside = !inside;

    uint64 any = 0;

    for (int32 v = 0; v < axes.height; ++v) {
        uint64 own  = 0;
        uint64 front = 0;

        switch (axis_of(face)) {
            case 0:
                own   = occupancy.zrow(v, d);
                front = inside ? occupancy.zrow(v, nd) : boundary_row(src, face, v, axes.width);
                break;
            case 1:
                own   = occupancy.row(d, v);
                front = inside ? occupancy.row(nd, v) : boundary_row(src, face, v, axes.width);
                break;
            default:
                own   = occupancy.row(v, d);
                front = inside ? occupancy.row(v, nd) : boundary_row(src, face, v, axes.width);
                break;
        }

        out.front[v]   = front;
        out.own[v]     = own;
        out.visible[v] = own & ~front;
        any |= out.visible[v];
    }

    return any != 0;
}

struct corner_samples {
    uint64 edge_mu = 0;
    uint64 edge_pu = 0;
    uint64 edge_mv = 0;
    uint64 edge_pv = 0;
    uint64 diag_c0 = 0;
    uint64 diag_c1 = 0;
    uint64 diag_c2 = 0;
    uint64 diag_c3 = 0;
};

auto samples_from_rows(uint64 row_mv, uint64 row, uint64 row_pv) -> corner_samples {
    return {
        .edge_mu = row << 1,
        .edge_pu = row >> 1,
        .edge_mv = row_mv,
        .edge_pv = row_pv,
        .diag_c0 = row_mv << 1,
        .diag_c1 = row_mv >> 1,
        .diag_c2 = row_pv >> 1,
        .diag_c3 = row_pv << 1,
    };
}

auto pack_corners(const corner_samples& s, int u) -> uint8 {
    const auto bit = [u](uint64 mask) -> bool { return ((mask >> u) & 1U) != 0; };

    const uint8 c0 = corner_level(bit(s.edge_mu), bit(s.edge_mv), bit(s.diag_c0));
    const uint8 c1 = corner_level(bit(s.edge_pu), bit(s.edge_mv), bit(s.diag_c1));
    const uint8 c2 = corner_level(bit(s.edge_pu), bit(s.edge_pv), bit(s.diag_c2));
    const uint8 c3 = corner_level(bit(s.edge_mu), bit(s.edge_pv), bit(s.diag_c3));

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

auto pack_corners_convex(const corner_samples& s, int u) -> uint8 {
    const auto open = [u](uint64 mask) -> bool { return ((mask >> u) & 1U) == 0; };

    const uint8 c0 = corner_open_level(open(s.edge_mu), open(s.edge_mv), open(s.diag_c0));
    const uint8 c1 = corner_open_level(open(s.edge_pu), open(s.edge_mv), open(s.diag_c1));
    const uint8 c2 = corner_open_level(open(s.edge_pu), open(s.edge_pv), open(s.diag_c2));
    const uint8 c3 = corner_open_level(open(s.edge_mu), open(s.edge_pv), open(s.diag_c3));

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

auto emit_rect(
    mesh_generation_storage& storage,
    const face_axis_mapping& axes,
    face_direction face,
    int layer,
    int u_start,
    int v_start,
    int w,
    int h,
    const face_mask_cell& cell
) -> void {
    auto [min_pos, max_pos] = axes.to_local_min_max(u_start, v_start, w, h, layer);

    add_quad(
        storage.quads,
        face,
        min_pos,
        max_pos,
        cell.index,
        cell.corner_ao,
        cell.corner_convex,
        cell.light
    );
}

[[nodiscard]] auto cell_span_mask(int32 step) -> uint64 {
    return (uint64{1} << step) - 1;
}

[[nodiscard]] auto compress_row(uint64 bits, int32 step, int32 cells) -> uint64 {
    const uint64 span = cell_span_mask(step);

    uint64 out = 0;
    for (int32 c = 0; c < cells; ++c) {
        if (((bits >> (c * step)) & span) != 0) {
            out |= uint64{1} << c;
        }
    }
    return out;
}

// см. docs/lod-plan.md#швы
[[nodiscard]] auto compress_row_full(uint64 bits, int32 step, int32 cells) -> uint64 {
    const uint64 span = cell_span_mask(step);

    uint64 out = 0;
    for (int32 c = 0; c < cells; ++c) {
        if (((bits >> (c * step)) & span) == span) {
            out |= uint64{1} << c;
        }
    }
    return out;
}

auto build_cell_occupancy(
    const vw::asset::chunk_occupancy& fine, int32 step, vw::asset::chunk_occupancy& out
) -> void {
    const int32 cells = vw::asset::chunk_occupancy::side / step;

    out.clear();

    for (int32 cy = 0; cy < cells; ++cy) {
        for (int32 cz = 0; cz < cells; ++cz) {
            uint64 merged = 0;
            for (int32 dy = 0; dy < step; ++dy) {
                for (int32 dz = 0; dz < step; ++dz) {
                    merged |= fine.row((cy * step) + dy, (cz * step) + dz);
                }
            }
            out.set_row(cy, cz, compress_row(merged, step, cells));
        }

        for (int32 cx = 0; cx < cells; ++cx) {
            uint64 merged = 0;
            for (int32 dy = 0; dy < step; ++dy) {
                for (int32 dx = 0; dx < step; ++dx) {
                    merged |= fine.zrow((cy * step) + dy, (cx * step) + dx);
                }
            }
            out.set_zrow(cy, cx, compress_row(merged, step, cells));
        }
    }
}

auto build_cell_indices(
    const vw::asset::model& voxels, const vw::asset::chunk_occupancy& fine, int32 step,
    std::vector<voxel>& out
) -> void {
    const int32 cells = vw::asset::chunk_occupancy::side / step;
    const uint64 span = cell_span_mask(step);

    out.assign(
        static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells) *
            static_cast<std::size_t>(cells),
        voxel{}
    );

    for (int32 cz = 0; cz < cells; ++cz) {
        for (int32 cy = 0; cy < cells; ++cy) {
            for (int32 cx = 0; cx < cells; ++cx) {
                voxel pick{};

                for (int32 dy = step - 1; dy >= 0 && pick.is_empty(); --dy) {
                    for (int32 dz = 0; dz < step && pick.is_empty(); ++dz) {
                        const int32 fy    = (cy * step) + dy;
                        const int32 fz    = (cz * step) + dz;
                        const uint64 bits = (fine.row(fy, fz) >> (cx * step)) & span;
                        if (bits == 0) {
                            continue;
                        }
                        pick = voxels.get_voxel(
                            (cx * step) + static_cast<int32>(std::countr_zero(bits)), fy, fz
                        );
                    }
                }

                const auto at = (((static_cast<std::size_t>(cz) * cells) + cy) * cells) + cx;
                out[at]       = pick;
            }
        }
    }
}

auto build_cell_boundary(
    const vw::asset::model_boundary& fine, int32 step, vw::asset::model_boundary& out
) -> void {
    const int32 cells = vw::asset::face_occupancy::side / step;

    out.valid = fine.valid;

    for (const face_direction face : all_face_directions) {
        auto& plane = out.faces[face];
        plane.clear();

        if ((fine.valid & face_bit(face)) == 0) {
            continue;
        }

        const auto& source = fine.faces[face];
        for (int32 b = 0; b < cells; ++b) {
            uint64 covered = ~uint64{0};
            for (int32 db = 0; db < step; ++db) {
                covered &= source.rows[(b * step) + db];
            }
            plane.rows[b] = compress_row_full(covered, step, cells);
        }
    }
}

}  // namespace detail


auto simple_mesh_generator::generate_mesh_data(
    mesh_source src, mesh_options opts
) -> mesh {
    std::vector<quad> quads;
    std::array<uint32, 6> face_counts{};

    for (const face_direction face : all_face_directions) {
        const auto before = quads.size();

        for (int x = 0; x < src.voxels.width(); x++) {
            for (int y = 0; y < src.voxels.height(); y++) {
                for (int z = 0; z < src.voxels.depth(); z++) {
                    if (const voxel id = src.voxels.get_voxel(x, y, z);
                        !id.is_empty()) {
                        if (is_face_visible(src, x, y, z, face)) {
                            add_cube_face(
                                quads, src, x, y, z, face, id, opts
                            );
                        }
                    }
                }
            }
        }

        face_counts[std::to_underlying(face)] = static_cast<uint32>(quads.size() - before);
    }

    return mesh{.quads = std::move(quads), .face_counts = face_counts};
}

auto simple_mesh_generator::add_cube_face(
    std::vector<quad>& quads,
    mesh_source src,
    int x,
    int y,
    int z,
    face_direction face,
    voxel voxel_id,
    [[maybe_unused]] mesh_options opts
) -> void {
    detail::add_quad(
        quads,
        face,
        {x, y, z},
        {x + 1, y + 1, z + 1},
        voxel_id,
        detail::compute_corner_darkness(src, x, y, z, face),
        detail::compute_corner_convexity(src, x, y, z, face),
        detail::compute_corner_light(src, x, y, z, face)
    );
}

auto simple_mesh_generator::is_face_visible(
    mesh_source src, int x, int y, int z, face_direction face
) -> bool {
    const vec3i step = offset_of(face);

    int nx = x + step.x;
    int ny = y + step.y;
    int nz = z + step.z;

    if (nx < 0 || nx >= src.voxels.width() || ny < 0 || ny >= src.voxels.height() || nz < 0 ||
        nz >= src.voxels.depth()) {
        return true;
    }

    return src.voxels.is_empty(nx, ny, nz);
}


auto strip_mesh_generator::generate_mesh_data(
    mesh_generation_storage& storage,
    mesh_source src,
    mesh_options opts
) -> mesh {
    storage.clear();

    auto total    = src.voxels.width() * src.voxels.height() * src.voxels.depth();
    auto estimate = static_cast<std::size_t>(total / 4);

    if (storage.quads.capacity() < estimate) {
        storage.quads.reserve(estimate);
    }

    std::array<uint32, 6> face_counts{};

    for (const face_direction face : all_face_directions) {
        const auto before = storage.quads.size();
        generate_face_quads(storage, src, face, opts);
        face_counts[std::to_underlying(face)] = static_cast<uint32>(storage.quads.size() - before);
    }

    return mesh{std::vector<quad>{storage.quads}, face_counts, {}};
}

auto strip_mesh_generator::merge_and_emit_strips(
    mesh_generation_storage& storage,
    const detail::face_axis_mapping& axes,
    face_direction face,
    int layer,
    [[maybe_unused]] mesh_options opts
) -> void {
    auto idx = [&](int u, int v) -> std::size_t {
        return static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height) + static_cast<std::size_t>(v);
    };

    for (int v = 0; v < axes.height; v++) {
        int u = 0;
        while (u < axes.width) {
            face_mask_cell cell = storage.mask[idx(u, v)];
            if (cell.is_empty()) {
                u++;
                continue;
            }

            int strip_start = u;
            u++;
            while (u < axes.width && storage.mask[idx(u, v)] == cell) {
                u++;
            }
            int w = u - strip_start;

            detail::emit_rect(
                storage, axes, face, layer, strip_start, v, w, 1, cell
            );
        }
    }
}

auto strip_mesh_generator::generate_face_quads(
    mesh_generation_storage& storage,
    mesh_source src,
    face_direction face,
    mesh_options opts
) -> void {
    detail::face_axis_mapping axes(src, face);
    constexpr int ps = vw::asset::model::page_size;

    auto mask_size = static_cast<std::size_t>(axes.width) * static_cast<std::size_t>(axes.height);
    storage.mask.resize(mask_size);

    const int depth_pages = (axes.depth + ps - 1) / ps;
    const int u_pages     = (axes.width + ps - 1) / ps;
    const int v_pages     = (axes.height + ps - 1) / ps;

    storage.depth_has_pages.resize(depth_pages);
    std::ranges::fill(storage.depth_has_pages, false);
    for (int pd = 0; pd < depth_pages; pd++) {
        for (int pu = 0; pu < u_pages && !storage.depth_has_pages[pd]; pu++) {
            for (int pv = 0; pv < v_pages && !storage.depth_has_pages[pd]; pv++) {
                auto [mx, my, mz] = axes.to_model_coords(pu * ps, pv * ps, pd * ps);
                if (src.voxels.get_page_mode(mx / ps, my / ps, mz / ps) != vw::asset::page_mode::empty) {
                    storage.depth_has_pages[pd] = true;
                }
            }
        }
    }

    for (int layer = 0; layer < axes.depth; layer++) {
        if (!storage.depth_has_pages[layer / ps]) {
            layer = ((layer / ps) + 1) * ps - 1;
            continue;
        }

        detail::build_face_mask(storage, src, axes, face, layer, opts);

        bool has_faces = false;
        for (std::size_t i = 0; i < mask_size && !has_faces; i++) {
            has_faces = !storage.mask[i].is_empty();
        }
        if (!has_faces)
            continue;

        merge_and_emit_strips(storage, axes, face, layer, opts);
    }
}


auto greedy_mesh_generator::generate_mesh_data(
    mesh_generation_storage& storage,
    mesh_source src,
    mesh_options opts
) -> mesh {
    storage.clear();

    constexpr std::size_t estimate = 4096;

    if (storage.quads.capacity() < estimate) {
        storage.quads.reserve(estimate);
    }

    if (!storage.occupancy) {
        storage.occupancy = std::make_unique<vw::asset::chunk_occupancy>();
    }
    storage.occupancy_valid = src.voxels.build_occupancy(*storage.occupancy);

    const int32 step = effective_lod_step(src.voxels, opts.lod_step);

    if (step > 1 && storage.occupancy_valid) {
        if (!storage.lod_cells) {
            storage.lod_cells = std::make_unique<vw::asset::chunk_occupancy>();
        }

        detail::build_cell_occupancy(*storage.occupancy, step, *storage.lod_cells);
        detail::build_cell_indices(src.voxels, *storage.occupancy, step, storage.lod_indices);

        if (src.boundary != nullptr) {
            detail::build_cell_boundary(*src.boundary, step, storage.lod_boundary);
            src.boundary = &storage.lod_boundary;
        }

        src.lod_step    = step;
        src.lod_cells   = storage.lod_cells.get();
        src.lod_indices = storage.lod_indices.data();
    }

    std::array<uint32, 6> face_counts{};

    for (const face_direction face : all_face_directions) {
        const auto before = storage.quads.size();
        generate_face_quads(storage, src, face, opts);
        face_counts[std::to_underlying(face)] = static_cast<uint32>(storage.quads.size() - before);
    }

    vw::asset::chunk_links links;
    if (opts.build_links && storage.occupancy_valid) {
        links = vw::asset::build_chunk_links(*storage.occupancy, storage.link_scratch);
    } else {
        for (auto& cell : links.cells) {
            cell.pockets.assign(1, vw::asset::chunk_pocket::wide_open());
        }
    }

    return mesh{std::vector<quad>{storage.quads}, face_counts, std::move(links), src.lod_step};
}

auto greedy_mesh_generator::merge_and_emit_rects_bits(
    mesh_generation_storage& storage,
    const detail::face_axis_mapping& axes,
    face_direction face,
    int layer,
    detail::layer_rows& rows
) -> void {
    auto idx = [&](int u, int v) -> std::size_t {
        return (static_cast<std::size_t>(v) * static_cast<std::size_t>(axes.width)) +
               static_cast<std::size_t>(u);
    };

    const auto keys_match = [&](int v, int u, int w, const face_mask_cell& key) -> bool {
        for (int du = 0; du < w; ++du) {
            if (storage.mask[idx(u + du, v)] != key) {
                return false;
            }
        }
        return true;
    };

    for (int v = 0; v < axes.height; ++v) {
        uint64 row = rows.visible[v];

        while (row != 0) {
            const int u = std::countr_zero(row);
            const face_mask_cell key = storage.mask[idx(u, v)];

            int w = 1;
            while (u + w < axes.width && ((row >> (u + w)) & 1U) != 0 &&
                   storage.mask[idx(u + w, v)] == key) {
                ++w;
            }

            const uint64 span =
                (w == 64) ? ~uint64{0} : (((uint64{1} << w) - 1) << u);

            int h = 1;
            while (v + h < axes.height && (rows.visible[v + h] & span) == span &&
                   keys_match(v + h, u, w, key)) {
                rows.visible[v + h] &= ~span;
                ++h;
            }

            row &= ~span;
            detail::emit_rect(storage, axes, face, layer, u, v, w, h, key);
        }

        rows.visible[v] = 0;
    }
}

auto greedy_mesh_generator::merge_and_emit_rects(
    mesh_generation_storage& storage,
    const detail::face_axis_mapping& axes,
    face_direction face,
    int layer,
    [[maybe_unused]] mesh_options opts
) -> void {
    auto idx = [&](int u, int v) -> std::size_t {
        return static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height) + static_cast<std::size_t>(v);
    };

    face_mask_cell empty_cell{voxel{}, 0};

    for (int v = 0; v < axes.height; v++) {
        for (int u = 0; u < axes.width; u++) {
            face_mask_cell cell = storage.mask[idx(u, v)];
            if (cell.is_empty())
                continue;

            int w = 1;
            while (u + w < axes.width && storage.mask[idx(u + w, v)] == cell) {
                w++;
            }

            int h = 1;
            while (v + h < axes.height) {
                bool row_ok = true;
                for (int du = 0; du < w; du++) {
                    if (storage.mask[idx(u + du, v + h)] != cell) {
                        row_ok = false;
                        break;
                    }
                }
                if (!row_ok)
                    break;
                h++;
            }

            for (int dv = 0; dv < h; dv++) {
                for (int du = 0; du < w; du++) {
                    storage.mask[idx(u + du, v + dv)] = empty_cell;
                }
            }

            detail::emit_rect(storage, axes, face, layer, u, v, w, h, cell);
        }
    }
}

auto greedy_mesh_generator::generate_face_quads(
    mesh_generation_storage& storage,
    mesh_source src,
    face_direction face,
    mesh_options opts
) -> void {
    detail::face_axis_mapping axes(src, face);
    constexpr int32 ps = vw::asset::model::page_size;

    const int32 cells_per_page = ps / axes.step;

    auto mask_size = static_cast<std::size_t>(axes.width) * static_cast<std::size_t>(axes.height);
    storage.mask.resize(mask_size);

    const int32 depth_pages = (axes.depth + cells_per_page - 1) / cells_per_page;
    const int32 u_pages     = (axes.width + cells_per_page - 1) / cells_per_page;
    const int32 v_pages     = (axes.height + cells_per_page - 1) / cells_per_page;

    storage.depth_has_pages.resize(depth_pages);
    std::ranges::fill(storage.depth_has_pages, false);
    for (int32 pd = 0; pd < depth_pages; pd++) {
        for (int32 pu = 0; pu < u_pages && !storage.depth_has_pages[pd]; pu++) {
            for (int32 pv = 0; pv < v_pages && !storage.depth_has_pages[pd]; pv++) {
                auto [mx, my, mz] = axes.to_model_coords(
                    pu * cells_per_page, pv * cells_per_page, pd * cells_per_page
                );
                const auto mode = src.voxels.get_page_mode(
                    (mx * axes.step) / ps, (my * axes.step) / ps, (mz * axes.step) / ps
                );
                if (mode != vw::asset::page_mode::empty) {
                    storage.depth_has_pages[pd] = true;
                }
            }
        }
    }

    auto idx = [&](int32 u, int32 v) -> std::size_t {
        return (static_cast<std::size_t>(v) * static_cast<std::size_t>(axes.width)) +
               static_cast<std::size_t>(u);
    };

    const auto& cells = axes.step > 1 ? *src.lod_cells : *storage.occupancy;

    detail::layer_rows rows;

    for (int32 layer = 0; layer < axes.depth; layer++) {
        if (!storage.depth_has_pages[layer / cells_per_page]) {
            layer = ((layer / cells_per_page) + 1) * cells_per_page - 1;
            continue;
        }

        if (storage.occupancy_valid) {
            if (!detail::build_layer_rows(src, cells, axes, face, layer, rows)) {
                continue;
            }

            for (int v = 0; v < axes.height; v++) {
                uint64 bits = rows.visible[v];
                if (bits == 0) {
                    continue;
                }

                const bool interior_v = v > 0 && v + 1 < axes.height;
                const bool bit_ao     = interior_v && !rows.front_outside;

                const auto samples = detail::samples_from_rows(
                    interior_v ? rows.front[v - 1] : 0,
                    rows.front[v],
                    interior_v ? rows.front[v + 1] : 0
                );

                const bool wants_convex = face == detail::convex_face;

                const auto own_samples = detail::samples_from_rows(
                    interior_v && wants_convex ? rows.own[v - 1] : 0,
                    wants_convex ? rows.own[v] : 0,
                    interior_v && wants_convex ? rows.own[v + 1] : 0
                );

                while (bits != 0) {
                    const int u = std::countr_zero(bits);
                    bits &= bits - 1;

                    auto [mx, my, mz] = axes.to_model_coords(u, v, layer);

                    const bool interior = bit_ao && u > 0 && u + 1 < axes.width;

                    const uint8 dark   = interior ? detail::pack_corners(samples, u)
                                                  : detail::compute_corner_darkness(
                                                        src, mx, my, mz, face
                                                    );
                    uint8 convex = 0;
                    if (wants_convex) {
                        convex = interior
                            ? detail::pack_corners_convex(own_samples, u)
                            : detail::compute_corner_convexity(src, mx, my, mz, face);
                    }
                    const corner_light light =
                        interior
                            ? detail::light_from_rows(src, rows, u, v, mx, my, mz, face)
                            : detail::compute_corner_light(src, mx, my, mz, face);

                    storage.mask[idx(u, v)] = {
                        src.cell_index(mx, my, mz), dark, light, convex
                    };
                }
            }

            merge_and_emit_rects_bits(storage, axes, face, layer, rows);
            continue;
        }

        detail::build_face_mask(storage, src, axes, face, layer, opts);

        bool has_faces = false;
        for (std::size_t i = 0; i < mask_size && !has_faces; i++) {
            has_faces = !storage.mask[i].is_empty();
        }
        if (!has_faces)
            continue;

        merge_and_emit_rects(storage, axes, face, layer, opts);
    }
}

}  // namespace vw::gfx
