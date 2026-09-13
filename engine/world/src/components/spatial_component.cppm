export module vw.world:components.spatial;

import std;

import vw.core;
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

struct spatial_component final {
    [[nodiscard]] auto get_bounds() const -> const spatial::aabb& {
        return bounds_;
    }

    [[nodiscard]] auto get_layer() const -> spatial_layer_mask {
        return layer_;
    }

    [[nodiscard]] auto is_dirty() const -> bool {
        return dirty_;
    }

private:
    friend class spatial_system;

    spatial::aabb bounds_;
    spatial::aabb fat_bounds_;
    spatial_layer_mask layer_ = spatial_layer::prop;
    bool dirty_               = true;
};

}  // namespace vw::ecs
