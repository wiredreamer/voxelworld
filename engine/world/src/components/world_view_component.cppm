export module vw.world:components.world_view;

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

// см. docs/light-plan.md#пресеты-качества
inline constexpr uint32 default_view_distance = 8;

struct world_view_component final {
    [[nodiscard]] auto get_chunk_coord() const -> vec3i {
        return chunk_coord_;
    }

    [[nodiscard]] auto get_view_distance() const -> uint32 {
        return view_distance_;
    }

private:
    friend class world_grid_system;

    vec3i chunk_coord_{0, 0, 0};
    uint32 view_distance_{default_view_distance};
    bool dirty_ = true;
};
}  // namespace vw::ecs
