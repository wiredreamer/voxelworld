export module vw.world:systems.character_controller;

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

class character_controller_system final {
public:
    static constexpr std::string_view system_name = "char_ctrl";

    explicit character_controller_system(world& w);

    auto update(float32 delta_time) -> void;

    class controller_modifier {
    public:
        auto set_move_input(const vec3f& input) -> controller_modifier&;
        auto set_facing_direction(const vec3f& direction) -> controller_modifier&;
        auto set_move_speed(float32 speed) -> controller_modifier&;
        auto set_acceleration_seconds(float32 seconds) -> controller_modifier&;
        auto set_deceleration_seconds(float32 seconds) -> controller_modifier&;
        auto set_jump_impulse(float32 impulse) -> controller_modifier&;
        auto set_turn_degrees_per_second(float32 degrees_per_second) -> controller_modifier&;
        auto set_coyote_seconds(float32 seconds) -> controller_modifier&;
        auto set_step_hop_voxels(float32 voxels) -> controller_modifier&;
        auto set_ride_lead(float32 lead_voxels, float32 rise_speed, float32 sink_speed)
            -> controller_modifier&;
        auto request_jump() -> controller_modifier&;

    private:
        friend class character_controller_system;
        controller_modifier(character_controller_system* system, entity ent);

        character_controller_system* system_;
        entity entity_;
    };

    auto modify(entity ent) -> controller_modifier;

private:

    world* world_;
};

}  // namespace vw::ecs
