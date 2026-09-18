module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

chunk::chunk(
    world& w, vec3i coord, std::shared_ptr<asset::chunk_volume> content, int32 world_units_per_voxel
)
    : world_(&w)
    , coord_(coord)
    , world_units_per_voxel_(world_units_per_voxel)
    , ent_(invalid_entity)
    , volume_(std::move(content))
    , fill_(volume_->voxels().scan_fill()) {
    for (const face_direction face : all_face_directions) {
        if (volume_->has_boundary_slice(face)) {
            known_neighbors_ |= face_bit(face);
        }
    }

    const bool has_faces = fill_ == asset::model_fill::mixed ||
        (fill_ == asset::model_fill::solid && !volume_->boundaries_are_solid());

    if (has_faces) {
        create_entity_();
        return;
    }

    volume_->release_boundary();
}

auto chunk::create_entity_() -> void {
    auto& w = *world_;

    ent_ = w.create()
        .with<transform_component>()
        .with<model_component>()
        .with<spatial_component>()
        .get_entity();

    auto world_pos = vec3f{
        static_cast<float32>(coord_.x * size * world_units_per_voxel_),
        static_cast<float32>(coord_.y * size * world_units_per_voxel_),
        static_cast<float32>(coord_.z * size * world_units_per_voxel_)
    };

    auto vs = static_cast<float32>(world_units_per_voxel_);
    w.system<transform_system>().modify(ent_)
        .set_position(world_pos)
        .set_scale({vs, vs, vs});
    w.system<model_system>().modify(ent_).set_chunk(volume_);
    w.system<spatial_system>().modify(ent_).set_layer(spatial_layer::terrain);
}

auto chunk::ensure_entity() -> bool {
    if (world_ == nullptr || ent_.is_valid()) {
        return false;
    }

    create_entity_();
    return true;
}

chunk::~chunk() {
    if (world_ != nullptr && ent_.is_valid()) {
        world_->destroy(ent_);
    }
}

chunk::chunk(
    chunk&& other
) noexcept
    : world_(other.world_)
    , coord_(other.coord_)
    , world_units_per_voxel_(other.world_units_per_voxel_)
    , ent_(other.ent_)
    , volume_(std::move(other.volume_))
    , fill_(other.fill_)
    , known_neighbors_(other.known_neighbors_) {
    other.world_ = nullptr;
    other.ent_   = invalid_entity;
}

auto chunk::operator=(
    chunk&& other
) noexcept -> chunk& {
    if (this != &other) {
        if (world_ != nullptr && ent_.is_valid()) {
            world_->destroy(ent_);
        }
        world_       = other.world_;
        coord_       = other.coord_;
        world_units_per_voxel_ = other.world_units_per_voxel_;
        ent_             = other.ent_;
        volume_          = std::move(other.volume_);
        fill_            = other.fill_;
        known_neighbors_ = other.known_neighbors_;
        other.world_ = nullptr;
        other.ent_   = invalid_entity;
    }
    return *this;
}

auto chunk::get_voxel(
    int32 x, int32 y, int32 z
) const -> voxel {
    return volume_->voxels().get_voxel(x, y, z);
}

auto chunk::get_voxel(
    vec3i local
) const -> voxel {
    return volume_->voxels().get_voxel(local);
}

auto chunk::set_voxel(
    int32 x, int32 y, int32 z, voxel v
) -> void {
    volume_->voxels().set_voxel(x, y, z, v);
    fill_ = volume_->voxels().scan_fill();
}

auto chunk::set_voxel(
    vec3i local, voxel v
) -> void {
    set_voxel(local.x, local.y, local.z, v);
}

auto chunk::is_empty(
    int32 x, int32 y, int32 z
) const -> bool {
    return volume_->voxels().is_empty(x, y, z);
}

auto chunk::get_model() const -> const std::shared_ptr<asset::model>& {
    return volume_->shared_voxels();
}

auto chunk::get_volume() const -> const std::shared_ptr<asset::chunk_volume>& {
    return volume_;
}

auto chunk::get_entity() const -> entity {
    return ent_;
}

auto chunk::is_drawn() const -> bool {
    return ent_.is_valid();
}

auto chunk::is_solid() const -> bool {
    return fill_ == asset::model_fill::solid;
}

auto chunk::known_neighbors() const -> uint8 {
    return known_neighbors_;
}

auto chunk::set_known_neighbors(uint8 mask) -> void {
    known_neighbors_ = mask;
}

}  // namespace vw::ecs
