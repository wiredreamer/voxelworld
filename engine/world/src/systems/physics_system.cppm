export module vw.world:systems.physics;

import std;

import vw.core;
import vw.ecs;
import :components;
import :grid;
import :spatial;
import :light;
import :terrain;

export namespace vw::ecs {

class world;

struct physics_stats {
    float32 step_ms             = 0.0F;
    float32 voxel_collision_ms  = 0.0F;
    float32 entity_collision_ms = 0.0F;
    float32 entity_query_ms     = 0.0F;
    float32 entity_resolve_ms   = 0.0F;
    int32 step_count            = 0;
    int32 entity_query_results  = 0;
};

class physics_system final {
public:
    static constexpr std::string_view system_name = "physics";

    static constexpr int32 max_collision_iterations = 4;
    static constexpr float32 fixed_dt               = 1.0F / 60.0F;
    static constexpr int32 max_steps_per_frame      = 5;
    static constexpr float32 max_substep_voxels     = 0.5F;
    static constexpr float32 step_blocked_share     = 0.02F;
    static constexpr int32 step_lead_halvings       = 6;
    static constexpr float32 step_lead_slack        = 1.5F;

    explicit physics_system(world& w);

    auto set_gravity(float32 g) -> void;
    [[nodiscard]] auto get_gravity() const -> float32;

    auto update(float32 delta_time) -> void;
    [[nodiscard]] auto get_stats() const -> const physics_stats&;

    auto request_detailed_stats() -> void;

    class rigid_body_modifier {
    public:
        auto set_velocity(const vec3f& vel) -> rigid_body_modifier&;
        auto set_gravity_scale(float32 scale) -> rigid_body_modifier&;
        auto add_impulse(const vec3f& impulse) -> rigid_body_modifier&;
        auto add_external_impulse(const vec3f& impulse) -> rigid_body_modifier&;
        auto set_drag(float32 drag) -> rigid_body_modifier&;
        auto smooth_steps(entity follower, float32 seconds) -> rigid_body_modifier&;

    private:
        friend class physics_system;
        rigid_body_modifier(physics_system* system, entity ent);

        physics_system* system_;
        entity entity_;
    };

    class collider_modifier {
    public:
        auto set_extents(const vec3f& ext) -> collider_modifier&;
        auto set_offset(const vec3f& offset) -> collider_modifier&;

    private:
        friend class physics_system;
        collider_modifier(physics_system* system, entity ent);

        physics_system* system_;
        entity entity_;
    };

    auto modify(entity ent) -> rigid_body_modifier;
    auto modify_collider(entity ent) -> collider_modifier;

private:
    auto step(float32 dt) -> void;
    [[nodiscard]] auto are_chunks_loaded(const vec3f& position, const vec3f& extents) const -> bool;

    struct collision_result {
        vec3f resolved_position;
        bool grounded = false;
    };

    [[nodiscard]] auto resolve_box_voxel(vec3f center, const vec3f& half_extents,
                                         vec3f& velocity) const -> collision_result;
    [[nodiscard]] auto step_up_(
        const vec3f& center, const vec3f& half_extents, const vec3f& wanted, float32 step_height
    ) const -> std::optional<vec3f>;
    [[nodiscard]] auto rise_coming_(
        const vec3f& center, const vec3f& half_extents, const vec3f& heading, float32 step_height,
        float32 within
    ) const -> float32;
    [[nodiscard]] auto box_blocked_(const vec3f& lo, const vec3f& hi) const -> bool;

    auto resolve_entity_collisions(entity ent, vec3f& position, vec3f& velocity,
                                   const vec3f& half_extents, const vec3f& offset) -> void;

    world* world_;
    float32 gravity_          = -300.0F;
    float32 accumulated_time_ = 0.0F;
    bool detailed_requested_  = false;
    bool detailed_active_     = false;
    physics_stats stats_;
    std::vector<entity> entity_query_cache_;
};

}  // namespace vw::ecs
