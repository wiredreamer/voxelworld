export module vw.world:components.model;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :spatial;

export namespace vw::ecs {

class animation_fsm_system;
class animation_system;
class character_controller_system;
class hierarchy_system;
class light_system;
class model_system;
class physics_system;
class socket_system;
class spatial_system;
class transform_system;
class world_grid_system;

struct model_component final {
    [[nodiscard]] auto has_model() const -> bool {
        return model_ != nullptr;
    }

    [[nodiscard]] auto get_model() const -> std::shared_ptr<asset::model> {
        return model_;
    }

    // Чанк, которым эта модель стоит в мире, — со швами и запечённым светом; у
    // модели, которая чанком не является (всё в редакторе), его нет вовсе, и
    // мешер строит её как отдельно стоящую.
    [[nodiscard]] auto get_chunk() const -> const std::shared_ptr<asset::chunk_volume>& {
        return chunk_;
    }

    [[nodiscard]] auto get_voxel(int32 x, int32 y, int32 z) const -> voxel {
        return model_->get_voxel(x, y, z);
    }

    [[nodiscard]] auto get_voxel(vec3i pos) const -> voxel {
        return model_->get_voxel(pos);
    }

    [[nodiscard]] auto is_empty(int32 x, int32 y, int32 z) const -> bool {
        return model_->is_empty(x, y, z);
    }

    [[nodiscard]] auto is_empty(vec3i pos) const -> bool {
        return model_->is_empty(pos);
    }

    [[nodiscard]] auto width() const -> int32 {
        return model_->width();
    }

    [[nodiscard]] auto height() const -> int32 {
        return model_->height();
    }

    [[nodiscard]] auto depth() const -> int32 {
        return model_->depth();
    }

    [[nodiscard]] auto size() const -> vec3i {
        return model_->size();
    }

    [[nodiscard]] auto get_identity() const -> asset::model_identity {
        return model_->get_identity();
    }

private:
    friend class model_system;

    std::shared_ptr<asset::model> model_;
    std::shared_ptr<asset::chunk_volume> chunk_;
};

}  // namespace vw::ecs
