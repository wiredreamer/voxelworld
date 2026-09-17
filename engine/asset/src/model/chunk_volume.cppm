export module vw.asset:model.chunk;

import std;

import vw.core;
import :model.occupancy;
import :model.light_field;
import :model.volume;

export namespace vw::asset {

class chunk_volume {
public:
    explicit chunk_volume(std::shared_ptr<model> voxels) : voxels_{std::move(voxels)} {}

    [[nodiscard]] auto voxels() -> model& {
        return *voxels_;
    }

    [[nodiscard]] auto voxels() const -> const model& {
        return *voxels_;
    }

    [[nodiscard]] auto shared_voxels() const -> const std::shared_ptr<model>& {
        return voxels_;
    }

    auto set_boundary_slice(face_direction face, const model& neighbor) -> void;

    [[nodiscard]] auto get_boundary_face(face_direction face) const -> const face_occupancy& {
        return boundary_->faces[face];
    }

    [[nodiscard]] auto has_boundary_slice(face_direction face) const -> bool {
        return boundary_ != nullptr && (boundary_->valid & face_bit(face)) != 0;
    }

    [[nodiscard]] auto is_boundary_solid(face_direction face, int32 x, int32 y, int32 z) const
        -> bool;

    [[nodiscard]] auto boundaries_are_solid() const -> bool;

    auto release_boundary() -> void {
        boundary_.reset();
    }

    auto set_sky_light(light_field light) -> void;

    [[nodiscard]] auto get_sky_light() const -> const light_field* {
        return sky_.get();
    }

    [[nodiscard]] auto has_sky_light() const -> bool {
        return sky_ != nullptr;
    }

    auto set_block_light(light_field light) -> void;

    [[nodiscard]] auto get_block_light() const -> const light_field* {
        return block_.get();
    }

    [[nodiscard]] auto has_block_light() const -> bool {
        return block_ != nullptr;
    }

private:
    std::shared_ptr<model> voxels_;
    std::unique_ptr<model_boundary> boundary_;
    std::unique_ptr<light_field> sky_;
    std::unique_ptr<light_field> block_;
};

}  // namespace vw::asset
