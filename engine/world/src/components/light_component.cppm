export module vw.world:components.light;

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

struct light_component final {
    [[nodiscard]] auto get_color() const -> const vec3f& {
        return color_;
    }

    [[nodiscard]] auto get_intensity() const -> float32 {
        return intensity_;
    }

    [[nodiscard]] auto get_range() const -> float32 {
        return range_;
    }

private:
    friend class light_system;

    vec3f color_{1.0F, 1.0F, 1.0F};
    float32 intensity_ = 1.0F;
    float32 range_     = 10.0F;
};

struct blob_shadow_component final {
    blob_shadow_component() = default;

    blob_shadow_component(float32 radius, float32 fall, float32 strength)
        : radius_{radius}, fall_{fall}, strength_{strength} {}

    [[nodiscard]] auto get_radius() const -> float32 {
        return radius_;
    }

    [[nodiscard]] auto get_fall() const -> float32 {
        return fall_;
    }

    [[nodiscard]] auto get_strength() const -> float32 {
        return strength_;
    }

private:
    float32 radius_   = 12.0F;
    float32 fall_     = 48.0F;
    float32 strength_ = 0.55F;
};

}  // namespace vw::ecs
