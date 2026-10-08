module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace {

[[nodiscard]] auto swaying_materials() -> const material_set& {
    static const material_set swaying = default_material_table().swaying();
    return swaying;
}

}  // namespace

auto chunk_volume::set_boundary_air(face_direction face) -> void {
    if (boundary_ == nullptr) {
        boundary_ = std::make_shared<model_boundary>();
    }

    boundary_->faces[face].clear();
    boundary_->leaf_faces[face].clear();
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

    auto& leaves = boundary_->leaf_faces[face];

    switch (neighbor.scan_fill()) {
        case model_fill::solid:
            plane.rows.fill(~uint64{0});
            leaves.clear();
            break;
        case model_fill::air:
            plane.clear();
            leaves.clear();
            break;
        case model_fill::mixed:
            static_cast<void>(neighbor.extract_face(opposite(face), plane, swaying_materials(), leaves));
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

}  // namespace vw::asset
