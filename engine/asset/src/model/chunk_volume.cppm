export module vw.asset:model.chunk;

import std;

import vw.core;
import :model.occupancy;
import :model.light_field;
import :model.cover;
import :model.volume;

export namespace vw::asset {

class chunk_volume {
public:
    explicit chunk_volume(std::shared_ptr<model> voxels, cover_layer cover = {})
        : voxels_{std::move(voxels)}, cover_{std::move(cover)} {}

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

    // см. docs/rendering.md#открытое-небо-это-не-отсутствие-данных
    auto set_boundary_air(face_direction face) -> void;

    // см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
    auto set_boundary_shell(vec3i step, const model& neighbor) -> void;

    auto set_boundary_shell_air(vec3i step) -> void;

    [[nodiscard]] auto get_boundary_face(face_direction face) const -> const face_occupancy& {
        return boundary_->faces[face];
    }

    [[nodiscard]] auto has_boundary_slice(face_direction face) const -> bool {
        return boundary_ != nullptr && (boundary_->valid & face_bit(face)) != 0;
    }

    [[nodiscard]] auto has_boundary_shell(vec3i step) const -> bool {
        if (boundary_ == nullptr) {
            return false;
        }
        switch (shell_span(step)) {
            case 1:
                return (boundary_->valid & face_bit(shell_face(step))) != 0;
            case 2:
                return boundary_->has_edge(step);
            default:
                return boundary_->has_corner(step);
        }
    }

    [[nodiscard]] auto is_boundary_solid(face_direction face, int32 x, int32 y, int32 z) const
        -> bool;

    [[nodiscard]] auto boundaries_are_solid() const -> bool;

    auto release_boundary() -> void {
        boundary_.reset();
    }

    [[nodiscard]] auto share_boundary() const -> std::shared_ptr<const model_boundary> {
        return boundary_;
    }

    [[nodiscard]] auto share_sky_light() const -> std::shared_ptr<const light_field> {
        return sky_;
    }

    [[nodiscard]] auto share_block_light() const -> std::shared_ptr<const light_field> {
        return block_;
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

    [[nodiscard]] auto cover() -> cover_layer& {
        return cover_;
    }

    [[nodiscard]] auto cover() const -> const cover_layer& {
        return cover_;
    }

private:
    std::shared_ptr<model> voxels_;
    cover_layer cover_;
    std::shared_ptr<model_boundary> boundary_;
    std::shared_ptr<light_field> sky_;
    std::shared_ptr<light_field> block_;
};

}  // namespace vw::asset
