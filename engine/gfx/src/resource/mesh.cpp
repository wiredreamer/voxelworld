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
    material made_of,
    uint8 state_code,
    bool sways
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
        (static_cast<uint32>(made_of.value) << material_shift);

    q.data1 =                                               //
        (span_u & 0x7Fu) |                                  //
        ((span_v & 0x7Fu) << 7) |                           //
        (static_cast<uint32>(v.value) << 14) |               //
        (sways ? sway_flag : 0U) |                           //
        (static_cast<uint32>(state_code) << state_shift);

    return q;
}

auto measured(
    mesh held
) -> mesh {
    if (held.quads.empty()) {
        return held;
    }

    constexpr int32 far = std::numeric_limits<int32>::max();

    vec3i lo{far, far, far};
    vec3i hi{-far, -far, -far};
    bool sways = false;

    for (const quad& q : held.quads) {
        const auto face = static_cast<face_direction>((q.data0 >> 21U) & 0x7U);

        const vec3i from{
            static_cast<int32>(q.data0 & 0x7FU), static_cast<int32>((q.data0 >> 7U) & 0x7FU),
            static_cast<int32>((q.data0 >> 14U) & 0x7FU)
        };
        vec3i to = from;
        to[axis_of(face)] += 1;
        to[quad::tangent_u_axis[face]] += static_cast<int32>(q.data1 & 0x7FU) + 1;
        to[quad::tangent_v_axis[face]] += static_cast<int32>((q.data1 >> 7U) & 0x7FU) + 1;

        lo    = vec3i{std::min(lo.x, from.x), std::min(lo.y, from.y), std::min(lo.z, from.z)};
        hi    = vec3i{std::max(hi.x, to.x), std::max(hi.y, to.y), std::max(hi.z, to.z)};
        sways = sways || (q.data1 & quad::sway_flag) != 0;
    }

    held.reach_min = lo;
    held.reach_max = hi;
    held.sways     = sways;
    return held;
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

[[nodiscard]] auto sways_at(mesh_source src, int32 x, int32 y, int32 z) -> bool;

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
    const face_mask_cell& cell,
    bool at_full_detail
) -> void;

struct layer_rows {
    std::array<uint64, 64> visible{};
};

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

auto sways_at(
    mesh_source src, int32 x, int32 y, int32 z
) -> bool {
    return default_material_table().get(src.voxels.get_material(x, y, z)).sways;
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
        if (!src.has_boundary_slice(face) || !src.covers_boundary_cell(face, x, y, z)) {
            return true;
        }
        if (src.lod_step != 1) {
            return false;
        }
        const vec2i on_plane = project_onto_face_plane(face, vec3i{x, y, z});
        return src.boundary->leaf_faces[face].test(on_plane.x, on_plane.y) != sways_at(src, x, y, z);
    }

    if (src.cell_empty(nx, ny, nz)) {
        return true;
    }

    // см. docs/rendering.md#качание-листвы
    return src.lod_step == 1 && sways_at(src, nx, ny, nz) != sways_at(src, x, y, z);
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

    constexpr face_mask_cell empty_cell{};

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
                                fid, src.cell_material(mx, my, mz), src.cell_state_code(mx, my, mz)
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
                            vx, src.cell_material(mx, my, mz), src.cell_state_code(mx, my, mz)
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
    const face_mask_cell& cell,
    bool at_full_detail
) -> void {
    quads.push_back(quad::pack(
        min_pos, max_pos, face, cell.index, cell.made_of, cell.state_code,
        at_full_detail && default_material_table().get(cell.made_of).sways
    ));
}


auto boundary_row(
    const vw::asset::face_occupancy& plane, face_direction face, int32 v, int32 width
) -> uint64 {

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

    const bool seam = !inside && src.has_boundary_slice(face);

    const auto across = [&](
                            const vw::asset::chunk_occupancy& cells,
                            const vw::asset::face_occupancy& plane, int32 v
                        ) -> std::pair<uint64, uint64> {
        const uint64 beyond = seam ? boundary_row(plane, face, v, axes.width) : 0;
        switch (axis_of(face)) {
            case 0:
                return {cells.zrow(v, d), inside ? cells.zrow(v, nd) : beyond};
            case 1:
                return {cells.row(d, v), inside ? cells.row(nd, v) : beyond};
            default:
                return {cells.row(v, d), inside ? cells.row(v, nd) : beyond};
        }
    };

    static const vw::asset::face_occupancy no_plane{};

    uint64 any = 0;

    for (int32 v = 0; v < axes.height; ++v) {
        const auto [own, front] = across(occupancy, seam ? src.boundary_face(face) : no_plane, v);

        // см. docs/rendering.md#качание-листвы
        uint64 parted = 0;
        if (src.leaves != nullptr) {
            const auto [own_leaf, front_leaf] =
                across(*src.leaves, seam ? src.boundary->leaf_faces[face] : no_plane, v);
            parted = own_leaf ^ front_leaf;
        }

        out.visible[v] = own & (~front | parted);
        any |= out.visible[v];
    }

    return any != 0;
}

// см. docs/rendering.md#качание-листвы
[[nodiscard]] auto sways(const face_mask_cell& cell, const face_axis_mapping& axes) -> bool {
    return axes.step == 1 && default_material_table().get(cell.made_of).sways;
}

[[nodiscard]] auto merge_reach(
    const face_mask_cell& cell, const face_axis_mapping& axes, int32 from, int32 extent
) -> int32 {
    if (!sways(cell, axes)) {
        return extent;
    }
    return std::min(extent, ((from / quad::sway_lattice) + 1) * quad::sway_lattice);
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

    add_quad(storage.quads, face, min_pos, max_pos, cell, axes.step == 1);
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
    std::vector<voxel>& out, std::vector<material>& made_of, std::vector<uint8>& state_codes
) -> void {
    const int32 cells = vw::asset::chunk_occupancy::side / step;
    const uint64 span = cell_span_mask(step);

    out.assign(
        static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells) *
            static_cast<std::size_t>(cells),
        voxel{}
    );
    made_of.assign(out.size(), material{});
    state_codes.assign(out.size(), uint8{0});

    for (int32 cz = 0; cz < cells; ++cz) {
        for (int32 cy = 0; cy < cells; ++cy) {
            for (int32 cx = 0; cx < cells; ++cx) {
                voxel pick{};
                material pick_made_of{};
                uint8 pick_state_code = 0;

                for (int32 dy = step - 1; dy >= 0 && pick.is_empty(); --dy) {
                    for (int32 dz = 0; dz < step && pick.is_empty(); ++dz) {
                        const int32 fy    = (cy * step) + dy;
                        const int32 fz    = (cz * step) + dz;
                        const uint64 bits = (fine.row(fy, fz) >> (cx * step)) & span;
                        if (bits == 0) {
                            continue;
                        }
                        const int32 fx = (cx * step) + static_cast<int32>(std::countr_zero(bits));
                        pick           = voxels.get_voxel(fx, fy, fz);
                        pick_made_of    = voxels.get_material(fx, fy, fz);
                        pick_state_code = shown_code(voxels.get_state(fx, fy, fz));
                    }
                }

                const auto at = (((static_cast<std::size_t>(cz) * cells) + cy) * cells) + cx;
                out[at]       = pick;
                made_of[at]   = pick_made_of;
                state_codes[at] = pick_state_code;
            }
        }
    }
}

auto build_cell_boundary(
    const vw::asset::model_boundary& fine, int32 step, vw::asset::model_boundary& covered
) -> void {
    const int32 cells = vw::asset::face_occupancy::side / step;

    covered.valid = fine.valid;

    for (const face_direction face : all_face_directions) {
        auto& covered_plane = covered.faces[face];

        covered_plane.clear();

        if ((fine.valid & face_bit(face)) == 0) {
            continue;
        }

        const auto& source = fine.faces[face];
        for (int32 b = 0; b < cells; ++b) {
            uint64 whole = ~uint64{0};
            for (int32 db = 0; db < step; ++db) {
                whole &= source.rows[(b * step) + db];
            }
            covered_plane.rows[b] = compress_row_full(whole, step, cells);
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

    return measured(mesh{.quads = std::move(quads), .face_counts = face_counts});
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
        quads, face, {x, y, z}, {x + 1, y + 1, z + 1},
        face_mask_cell{
            voxel_id, src.voxels.get_material(x, y, z), shown_code(src.voxels.get_state(x, y, z))
        },
        true
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

    return src.voxels.is_empty(nx, ny, nz) ||
           detail::sways_at(src, nx, ny, nz) != detail::sways_at(src, x, y, z);
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

    return measured(mesh{std::vector<quad>{storage.quads}, face_counts, {}});
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
            const int reach = detail::merge_reach(cell, axes, strip_start, axes.width);
            while (u < reach && storage.mask[idx(u, v)] == cell) {
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
        detail::build_cell_indices(
            src.voxels, *storage.occupancy, step, storage.lod_indices, storage.lod_materials,
            storage.lod_state_codes
        );

        if (src.boundary != nullptr) {
            detail::build_cell_boundary(*src.boundary, step, storage.lod_boundary);
            src.boundary = &storage.lod_boundary;
        }

        src.lod_step    = step;
        src.lod_cells   = storage.lod_cells.get();
        src.lod_indices   = storage.lod_indices.data();
        src.lod_materials   = storage.lod_materials.data();
        src.lod_state_codes = storage.lod_state_codes.data();
    }

    if (step == 1 && storage.occupancy_valid) {
        static const material_set leaf_set = default_material_table().swaying();
        if (!storage.leaves) {
            storage.leaves = std::make_unique<vw::asset::chunk_occupancy>();
        }
        const bool sways_inside = src.voxels.build_rows_of(*storage.leaves, leaf_set);
        const bool sways_beyond =
            src.boundary != nullptr &&
            std::ranges::any_of(all_face_directions, [&](face_direction face) {
                return src.has_boundary_slice(face) &&
                       std::ranges::any_of(src.boundary->leaf_faces[face].rows, [](uint64 row) {
                           return row != 0;
                       });
            });
        if (sways_inside || sways_beyond) {
            src.leaves = storage.leaves.get();
        }
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

    return measured(
        mesh{std::vector<quad>{storage.quads}, face_counts, std::move(links), src.lod_step}
    );
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
            const int reach_u        = detail::merge_reach(key, axes, u, axes.width);
            const int reach_v        = detail::merge_reach(key, axes, v, axes.height);

            int w = 1;
            while (u + w < reach_u && ((row >> (u + w)) & 1U) != 0 &&
                   storage.mask[idx(u + w, v)] == key) {
                ++w;
            }

            const uint64 span =
                (w == 64) ? ~uint64{0} : (((uint64{1} << w) - 1) << u);

            int h = 1;
            while (v + h < reach_v && (rows.visible[v + h] & span) == span &&
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

    face_mask_cell empty_cell{};

    for (int v = 0; v < axes.height; v++) {
        for (int u = 0; u < axes.width; u++) {
            face_mask_cell cell = storage.mask[idx(u, v)];
            if (cell.is_empty())
                continue;

            const int reach_u = detail::merge_reach(cell, axes, u, axes.width);
            const int reach_v = detail::merge_reach(cell, axes, v, axes.height);

            int w = 1;
            while (u + w < reach_u && storage.mask[idx(u + w, v)] == cell) {
                w++;
            }

            int h = 1;
            while (v + h < reach_v) {
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

                while (bits != 0) {
                    const int u = std::countr_zero(bits);
                    bits &= bits - 1;

                    auto [mx, my, mz] = axes.to_model_coords(u, v, layer);

                    storage.mask[idx(u, v)] = {
                        src.cell_index(mx, my, mz), src.cell_material(mx, my, mz),
                        src.cell_state_code(mx, my, mz)
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
