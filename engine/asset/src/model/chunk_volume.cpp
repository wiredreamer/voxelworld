module vw.asset;

import std;
import vw.core;

namespace vw::asset {

auto chunk_volume::set_boundary_air(face_direction face) -> void {
    if (boundary_ == nullptr) {
        boundary_ = std::make_shared<model_boundary>();
    }

    boundary_->faces[face].clear();
    boundary_->valid |= face_bit(face);
}

auto chunk_volume::set_boundary_slice(face_direction face, const model& neighbor) -> void {
    constexpr int32 side = face_occupancy::side;

    if (neighbor.width() != side || neighbor.height() != side || neighbor.depth() != side) {
        return;
    }

    if (boundary_ == nullptr) {
        boundary_ = std::make_shared<model_boundary>();
    }

    auto& plane = boundary_->faces[face];

    switch (neighbor.scan_fill()) {
        case model_fill::solid:
            plane.rows.fill(~uint64{0});
            break;
        case model_fill::air:
            plane.clear();
            break;
        case model_fill::mixed:
            static_cast<void>(neighbor.extract_face(opposite(face), plane));
            break;
    }

    boundary_->valid |= face_bit(face);
}

namespace {
[[nodiscard]] auto voxel_touching_us(int32 step) -> int32 {
    return step > 0 ? 0 : face_occupancy::side - 1;
}
}  // namespace

auto chunk_volume::set_boundary_shell(vec3i step, const model& neighbor) -> void {
    constexpr int32 side = face_occupancy::side;

    if (shell_span(step) == 1) {
        set_boundary_slice(shell_face(step), neighbor);
        return;
    }

    if (neighbor.width() != side || neighbor.height() != side || neighbor.depth() != side) {
        return;
    }

    if (boundary_ == nullptr) {
        boundary_ = std::make_shared<model_boundary>();
    }

    if (shell_span(step) == 2) {
        const int32 free = shell_free_axis(step);

        vec3i at{};
        for (int32 axis = 0; axis < 3; ++axis) {
            if (axis != free) {
                at[axis] = voxel_touching_us(step[axis]);
            }
        }

        uint64 bits = 0;
        for (int32 along = 0; along < side; ++along) {
            at[free] = along;
            if (!neighbor.is_empty(at.x, at.y, at.z)) {
                bits |= uint64{1} << along;
            }
        }

        const auto slot = static_cast<std::size_t>(shell_edge_index(step));

        boundary_->edges[slot] = bits;
        boundary_->edges_valid |= static_cast<uint16>(1U << slot);
        return;
    }

    const vec3i at{
        voxel_touching_us(step.x), voxel_touching_us(step.y), voxel_touching_us(step.z)
    };
    const auto slot = static_cast<uint8>(shell_corner_index(step));

    if (neighbor.is_empty(at.x, at.y, at.z)) {
        boundary_->corners &= static_cast<uint8>(~(1U << slot));
    } else {
        boundary_->corners |= static_cast<uint8>(1U << slot);
    }
    boundary_->corners_valid |= static_cast<uint8>(1U << slot);
}

auto chunk_volume::set_boundary_shell_air(vec3i step) -> void {
    if (shell_span(step) == 1) {
        set_boundary_air(shell_face(step));
        return;
    }

    if (boundary_ == nullptr) {
        boundary_ = std::make_shared<model_boundary>();
    }

    if (shell_span(step) == 2) {
        const auto slot = static_cast<std::size_t>(shell_edge_index(step));

        boundary_->edges[slot] = 0;
        boundary_->edges_valid |= static_cast<uint16>(1U << slot);
        return;
    }

    const auto slot = static_cast<uint8>(shell_corner_index(step));

    boundary_->corners &= static_cast<uint8>(~(1U << slot));
    boundary_->corners_valid |= static_cast<uint8>(1U << slot);
}

auto chunk_volume::is_boundary_solid(face_direction face, int32 x, int32 y, int32 z) const
    -> bool {
    const vec2i on_plane = project_onto_face_plane(face, vec3i{x, y, z});
    return boundary_->faces[face].test(on_plane.x, on_plane.y);
}

auto chunk_volume::boundaries_are_solid() const -> bool {
    if (boundary_ == nullptr || boundary_->valid != all_faces_mask) {
        return false;
    }

    const auto& mdl = *voxels_;
    if (mdl.width() != face_occupancy::side || mdl.height() != face_occupancy::side ||
        mdl.depth() != face_occupancy::side) {
        return false;
    }

    return std::ranges::all_of(boundary_->faces, [](const face_occupancy& face) -> bool {
        return std::ranges::all_of(face.rows, [](uint64 row) -> bool {
            return row == ~uint64{0};
        });
    });
}

auto chunk_volume::set_sky_light(light_field light) -> void {
    sky_ = std::make_shared<light_field>(std::move(light));
    voxels_->invalidate();
}

auto chunk_volume::set_block_light(light_field light) -> void {
    block_ = std::make_shared<light_field>(std::move(light));
    voxels_->invalidate();
}

}  // namespace vw::asset
