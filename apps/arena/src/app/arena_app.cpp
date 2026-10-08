module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;
import vw.platform;
import vw.gfx;

namespace vw::arena {

namespace {

constexpr float32 day_length_seconds = 300.0f;

auto player_shadow() -> ecs::blob_shadow_component {
    return ecs::blob_shadow_component{10.0f, 48.0f, 0.6f};
}

}  // namespace

arena_app::arena_app(
    gfx::engine& eng
)
    : app{eng}
    , model_library_(
          eng.get_world().resource<asset::model_registry>(), eng.get_voxel_registry(), "assets"
      )
    , assets_(parser_, model_library_)
    , camera_controller_(
          get_engine().get_camera(),
          get_engine().get_world(),
          gfx::third_person_camera_params{
              .arm_length     = 80.0f,
              .arm_length_min = 10.0f,
              .arm_length_max = 200.0f,
              .target_offset  = {0.0f, 36.5f, 0.0f},
              .zoom_speed     = 5.0f,
              .collision_skin = 2.0f
          }
      ) {
    get_engine().get_debug_tool().set_visible(true);
    day_night_.set_day_length_seconds(day_length_seconds);
    day_night_.apply(get_engine().get_renderer());

    load_assets();

    auto& world = get_engine().get_world();
    game::install_systems(world, assets_);

    load_input_bindings_();
    forward_input_();
    set_mouse_captured_(true);

    const auto result = setup_world_grid(get_engine());
    generator_params_ = result.generator_params;

    player_ = world.system<game::player_system>().spawn();
    world.modify(player_).with(player_shadow());
    world.system<ecs::transform_system>().modify(player_).set_position({0.0f, 500.0f, 0.0f});
    world.system<game::surface_placement_system>().place(player_, {0.0f, 0.0f});
    world.system<game::input_system>().control_locally(player_);
    register_debug_panels(get_engine(), player_, camera_controller_, day_night_);

    constexpr int32 enemy_count   = 10;
    constexpr float32 spawn_range = 400.0f;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution dist{-spawn_range, spawn_range};

    enemies_.reserve(enemy_count);
    for (int32 i = 0; i < enemy_count; ++i) {
        enemies_.push_back(
            std::make_unique<dummy_enemy>(get_engine(), vec2f{dist(rng), dist(rng)})
        );
    }

    const float32 draw_reach = static_cast<float32>(ecs::default_view_distance) *
                               static_cast<float32>(ecs::chunk::size) *
                               static_cast<float32>(generator_params_.world_units_per_voxel);

    auto& fog         = get_engine().get_renderer().get_fog_settings();
    fog.near_distance = ecs::fog_near_share * draw_reach;
    fog.far_distance  = ecs::fog_far_share * draw_reach;
}

auto arena_app::load_input_bindings_() -> void {
    constexpr log::log_category lc{"arena"};

    auto bindings = game::load_input_bindings("assets/data/input_bindings.json");
    if (!bindings) {
        log::warn(lc, "input layout not loaded, built-in keys are used: {}", bindings.error());
        return;
    }

    get_engine().get_world().system<game::input_system>().mapper().set_bindings(
        std::move(*bindings)
    );
}

auto arena_app::forward_input_() -> void {
    auto& window = get_engine().get_window();
    auto& mapper = get_engine().get_world().system<game::input_system>().mapper();

    window.sub<plat::key_press_event>([this, &mapper](const plat::key_press_event& event) -> bool {
        handle_key_press(event.key);
        mapper.key(event.key, true);
        return true;
    });
    window.sub<plat::key_release_event>([&mapper](const plat::key_release_event& event) -> bool {
        mapper.key(event.key, false);
        return false;
    });
    window.sub<plat::mouse_move_event>([this, &mapper](const plat::mouse_move_event& event) -> bool {
        if (mouse_captured_) {
            mapper.cursor_at(event.x, event.y);
        }
        return false;
    });
    window.sub<plat::mouse_press_event>([this, &mapper](const plat::mouse_press_event& event) -> bool {
        if (mouse_captured_) {
            mapper.button(event.button, true);
        }
        return false;
    });
    window.sub<plat::mouse_release_event>([&mapper](const plat::mouse_release_event& event) -> bool {
        mapper.button(event.button, false);
        return false;
    });
    window.sub<plat::mouse_scroll_event>([&mapper](const plat::mouse_scroll_event& event) -> bool {
        mapper.scroll(static_cast<float32>(event.offset_y));
        return false;
    });
    window.sub<plat::window_focus_event>([&mapper](const plat::window_focus_event& event) -> bool {
        if (!event.focused) {
            mapper.release_all();
        }
        return false;
    });
    window.sub<plat::window_close_event>([this](plat::window_close_event&) -> bool {
        get_engine().shutdown();
        return true;
    });
}

auto arena_app::set_mouse_captured_(
    bool captured
) -> void {
    auto& window = get_engine().get_window();

    mouse_captured_ = captured;
    window.set_cursor_mode(captured ? plat::cursor_modes::DISABLED : plat::cursor_modes::NORMAL);
    window.set_input_mode(plat::input_modes::RAW_MOUSE_MOTION, captured);

    get_engine().get_world().system<game::input_system>().mapper().drop_cursor();
}

auto arena_app::is_player_placed_() const -> bool {
    return !get_engine().get_world().system<game::surface_placement_system>().is_waiting(player_);
}

auto arena_app::render(
    float delta_time
) -> void {
    auto& world = get_engine().get_world();

    day_night_.tick(delta_time, get_engine().get_renderer());

    if (is_player_placed_()) {
        const auto& frame = world.get<game::player_input_component>(player_).get_frame();
        camera_controller_.update(
            player_, frame.look_yaw_degrees, frame.look_pitch_degrees, frame.zoom_delta,
            world.get<ecs::rigid_body_component>(player_).get_step_sink(),
            world.get<game::player_component>(player_).is_aiming(), delta_time
        );
        world.system<game::input_system>().aim_from(
            player_, get_engine().get_camera().get_position()
        );
    }

    if (show_colliders_) {
        get_engine().get_renderer().draw_colliders(world);
    }

    render_debug_hud(get_engine(), player_, show_colliders_);
}

auto arena_app::load_assets() -> void {
    assets_.load_prefab("p_humanoid", asset::asset_ref{"prefabs/p_humanoid.vox"});
    assets_.load_prefab("p_sword", asset::asset_ref{"prefabs/p_sword.vox"});
    assets_.load_prefab("p_shield", asset::asset_ref{"prefabs/p_shield.vox"});
    assets_.load_prefab("p_bow", asset::asset_ref{"prefabs/p_bow.vox"});
    assets_.load_prefab("p_arrow", asset::asset_ref{"prefabs/p_arrow.vox"});
}

auto arena_app::handle_key_press(
    plat::keyboard::keys key
) -> void {
    switch (key) {
        case plat::keyboard::keys::ESCAPE:
            get_engine().shutdown();
            break;
        case plat::keyboard::keys::F3:
            if (is_player_placed_()) {
                get_engine()
                    .get_world()
                    .system<ecs::physics_system>()
                    .modify(player_)
                    .add_external_impulse({0.0f, 0.0f, 500.0f});
            }
            break;
        case plat::keyboard::keys::F1:
            set_mouse_captured_(!mouse_captured_);
            break;
        case plat::keyboard::keys::F2:
            show_colliders_ = !show_colliders_;
            break;
        default:
            break;
    }
}

}  // namespace vw::arena
