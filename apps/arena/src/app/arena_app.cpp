module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::arena {

arena_app::arena_app(
    gfx::engine& eng
)
    : app{eng}
    // Корень ассетов у игры — папка assets рядом с исполняемым файлом: ссылки
    // внутри префабов те же, что у редактора, а корень у каждого свой.
    , model_library_(
          eng.get_world().resource<asset::model_registry>(), eng.get_block_registry(), "assets"
      )
    , assets_(parser_, model_library_)
    , input_controller_(get_engine().get_window())
    , camera_controller_(
          get_engine().get_camera(),
          get_engine().get_world(),
          gfx::third_person_camera_params{
              .arm_length     = 80.0f,
              .arm_length_min = 10.0f,
              .arm_length_max = 200.0f,
              .target_offset  = {0.0f, 24.0f, 0.0f},
              .pitch_min      = -30.0f,
              .pitch_max      = 80.0f,
              .zoom_speed     = 5.0f,
              .collision_skin = 2.0f
          }
      ) {
    auto& window = get_engine().get_window();
    window.sub<plat::key_press_event>([this](const plat::key_press_event& event) -> bool {
        handle_key_press(event.key);
        return true;
    });
    window.sub<plat::window_close_event>([this](plat::window_close_event&) -> bool {
        get_engine().shutdown();
        return true;
    });

    get_engine().get_renderer().set_clear_color(0.4f, 0.6f, 0.9f, 1.0f);
    get_engine().get_debug_tool().set_visible(true);

    input_controller_.set_mouse_captured(true);

    load_assets();

    const auto result = setup_world_grid(get_engine());
    generator_params_ = result.generator_params;

    player_ = std::make_unique<player>(get_engine(), assets_);

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

    auto& fog         = get_engine().get_renderer().get_fog_settings();
    fog.color         = {0.4f, 0.6f, 0.9f};
    fog.near_distance = 6.f * 64.f * generator_params_.voxel_scale;
    fog.far_distance  = 9.f * 64.f * generator_params_.voxel_scale;
}

auto arena_app::render(
    [[maybe_unused]] float delta_time
) -> void {
    if (!player_->is_placed()) {
        player_->try_place(static_cast<float32>(generator_params_.voxel_scale));
    }

    for (const auto& enemy : enemies_) {
        if (!enemy->is_placed()) {
            enemy->try_place();
        }
    }

    const auto input = input_controller_.get_input_state();

    if (player_->is_placed()) {
        const auto player_ent = player_->get_entity();
        camera_controller_.update(input, player_ent);
        player_->update(input);
    }

    if (show_colliders_) {
        get_engine().get_renderer().draw_colliders(get_engine().get_world());
    }

    render_debug_hud(get_engine(), *player_, camera_controller_, show_colliders_);
}

auto arena_app::load_assets() -> void {
    assets_.load_prefab("m_human", asset::asset_ref{"prefabs/m_human.vox"});
    assets_.load_prefab("m_sword", asset::asset_ref{"prefabs/m_sword.vox"});
}

auto arena_app::handle_key_press(
    plat::keyboard::keys key
) -> void {
    switch (key) {
        case plat::keyboard::keys::ESCAPE:
            get_engine().shutdown();
            break;
        case plat::keyboard::keys::KEY_1:
            player_->toggle_sword();
            break;
        case plat::keyboard::keys::F:
            if (player_->is_placed()) {
                get_engine()
                    .get_world()
                    .system<ecs::physics_system>()
                    .modify(player_->get_entity())
                    .add_external_impulse({0.0f, 0.0f, 500.0f});
            }
            break;
        case plat::keyboard::keys::F1:
            input_controller_.set_mouse_captured(!input_controller_.is_mouse_captured());
            break;
        case plat::keyboard::keys::F2:
            show_colliders_ = !show_colliders_;
            break;
        default:
            break;
    }
}


}  // namespace vw::arena
