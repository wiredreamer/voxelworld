export module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;
import vw.platform;
import vw.gfx;

export namespace vw::arena {

class dummy_enemy {
public:
    explicit dummy_enemy(gfx::engine& engine, const vec2f& spawn_xz);
    ~dummy_enemy();

    dummy_enemy(const dummy_enemy&)                    = delete;
    auto operator=(const dummy_enemy&) -> dummy_enemy& = delete;
    dummy_enemy(dummy_enemy&&)                         = delete;
    auto operator=(dummy_enemy&&) -> dummy_enemy&      = delete;

    [[nodiscard]] auto get_entity() const -> ecs::entity;

private:
    auto create_model() -> std::shared_ptr<asset::model>;

    gfx::engine& engine_;
    ecs::entity ent_;
    vec2f spawn_xz_;
};

}  // namespace vw::arena

export namespace vw::arena {

struct world_setup_result {
    ecs::perlin_terrain_generator::params generator_params;
};

auto setup_world_grid(gfx::engine& engine) -> world_setup_result;

}  // namespace vw::arena

export namespace vw::arena {

class arena_app final : public gfx::app {
public:
    explicit arena_app(gfx::engine& eng);
    ~arena_app() override = default;

    auto render(float delta_time) -> void override;

private:
    auto handle_key_press(plat::keyboard::keys key) -> void;
    auto load_assets() -> void;
    auto load_input_bindings_() -> void;
    auto forward_input_() -> void;
    auto set_mouse_captured_(bool captured) -> void;
    [[nodiscard]] auto is_player_placed_() const -> bool;

    asset::vox_parser_plain parser_;
    asset::model_library model_library_;
    asset::asset_storage assets_;

    gfx::third_person_camera_controller camera_controller_;
    gfx::day_night_cycle day_night_;

    ecs::entity player_;
    std::vector<std::unique_ptr<dummy_enemy>> enemies_;

    ecs::perlin_terrain_generator::params generator_params_;
    bool show_colliders_ = true;
    bool mouse_captured_ = false;
};

}  // namespace vw::arena

export namespace vw::arena {

auto register_debug_panels(
    gfx::engine& engine,
    ecs::entity player,
    const gfx::third_person_camera_controller& camera_controller,
    gfx::day_night_cycle& day_night
) -> void;

auto render_debug_hud(const gfx::engine& engine, ecs::entity player, bool show_colliders) -> void;

}  // namespace vw::arena
