export module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

// ---- from apps/arena/src/app/dummy_enemy.h
export namespace vw::arena {

class dummy_enemy {
public:
    explicit dummy_enemy(gfx::engine& engine, const vec2f& spawn_xz);
    ~dummy_enemy();

    dummy_enemy(const dummy_enemy&)                    = delete;
    auto operator=(const dummy_enemy&) -> dummy_enemy& = delete;
    dummy_enemy(dummy_enemy&&)                         = delete;
    auto operator=(dummy_enemy&&) -> dummy_enemy&      = delete;

    auto try_place() -> void;

    [[nodiscard]] auto get_entity() const -> ecs::entity;
    [[nodiscard]] auto is_placed() const -> bool;

private:
    auto create_model() -> std::shared_ptr<asset::model>;

    gfx::engine& engine_;
    ecs::entity ent_;
    vec2f spawn_xz_;
    bool placed_ = false;
};

}  // namespace vw::arena

// ---- from apps/arena/src/app/player.h
export namespace vw::arena {

class player {
public:
    explicit player(gfx::engine& engine, asset::asset_storage& assets);
    ~player();

    player(const player&)                    = delete;
    auto operator=(const player&) -> player& = delete;
    player(player&&)                         = delete;
    auto operator=(player&&) -> player&      = delete;

    auto update(const gfx::player_input_state& input) -> void;
    auto try_place(float32 voxel_scale) -> void;
    auto toggle_sword() -> void;

    [[nodiscard]] auto get_entity() const -> ecs::entity;
    [[nodiscard]] auto has_sword() const -> bool;
    [[nodiscard]] auto is_placed() const -> bool;

private:
    [[nodiscard]] auto create_body_part(
        std::string_view prefab_name, std::string_view part_name
    ) const -> ecs::entity;

    auto handle_attack() const -> void;
    [[nodiscard]] auto can_attack() const -> bool;
    auto setup_animation_fsm() const -> void;

    gfx::engine& engine_;
    asset::asset_storage& assets_;

    ecs::entity root_;
    ecs::entity body_;
    ecs::entity head_;
    ecs::entity hand_right_;
    ecs::entity hand_left_;
    ecs::entity foot_right_;
    ecs::entity foot_left_;
    ecs::entity sword_;

    static constexpr float32 default_rotation_speed_ = 5.0f;
    static constexpr float32 attack_rotation_speed_  = 25.0f;

    bool placed_            = false;
    bool need_update_jump_  = false;
    bool is_attacking_      = false;
    int32 jump_counter_     = 0;
    vec3f attack_facing_{0.0f, 0.0f, 0.0f};
};

}  // namespace vw::arena

// ---- from apps/arena/src/app/world_setup.h
export namespace vw::arena {

struct world_setup_result {
    ecs::perlin_terrain_generator::params generator_params;
};

auto setup_world_grid(gfx::engine& engine) -> world_setup_result;

}  // namespace vw::arena

// ---- from apps/arena/src/app/arena_app.h
export namespace vw::arena {

class arena_app final : public gfx::app {
public:
    explicit arena_app(gfx::engine& eng);
    ~arena_app() override = default;

    auto render(float delta_time) -> void override;

private:
    auto handle_key_press(plat::keyboard::keys key) -> void;
    auto load_assets() -> void;

    asset::vox_parser_plain parser_;
    asset::model_library model_library_;
    asset::asset_storage assets_;

    gfx::player_input_controller input_controller_;
    gfx::third_person_camera_controller camera_controller_;

    std::unique_ptr<player> player_;
    std::vector<std::unique_ptr<dummy_enemy>> enemies_;

    ecs::perlin_terrain_generator::params generator_params_;
    bool show_colliders_ = true;
};

}  // namespace vw::arena

// ---- from apps/arena/src/app/debug_hud.h
export namespace vw::arena {

auto render_debug_hud(
    const gfx::engine& engine,
    const player& player,
    const gfx::third_person_camera_controller& camera_controller,
    bool show_colliders
) -> void;

}  // namespace vw::arena
