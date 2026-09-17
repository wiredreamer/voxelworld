module vw.asset;

import std;
import vw.core;

namespace vw::asset {

auto chunk_volume::set_boundary_slice(face_direction face, const model& neighbor) -> void {
    constexpr int32 side = face_occupancy::side;

    if (neighbor.width() != side || neighbor.height() != side || neighbor.depth() != side) {
        return;
    }

    if (boundary_ == nullptr) {
        boundary_ = std::make_unique<model_boundary>();
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
    sky_ = std::make_unique<light_field>(std::move(light));
    voxels_->invalidate();
}

auto chunk_volume::set_block_light(light_field light) -> void {
    block_ = std::make_unique<light_field>(std::move(light));
    voxels_->invalidate();
}

}  // namespace vw::asset
