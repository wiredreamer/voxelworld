module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

testbed_app::testbed_app(
    gfx::engine& eng, const arg_reader& args, const scene_factory& make_scene,
    const camera_factory& make_camera
)
    : app{eng}
    , sun_in_bench_{args.flag("--sun")}
    , quality_{quality_of(args)}
    , view_distance_{
          args.count("--view-distance", gfx::preset_of(quality_).view_distance_columns)
      }
    , lod_distance_{args.real(
          "--lod-distance", static_cast<float32>(gfx::preset_of(quality_).lod_base_chunks)
      )}
    , lod_level_{args.text("--lod-level") ? args.integer("--lod-level", 0) : -1}
    , benching_{args.flag("--bench")}
    , clusters_{args.flag("--cluster-stats"), args.count("--verify-lights", 0)} {
    if (const auto shot = args.text("--shot")) {
        shot_path_ = std::filesystem::path{*shot};
    }
    if (args.text("--pitch")) {
        pitch_ = args.real("--pitch", 0.0f);
    }
    if (args.text("--yaw")) {
        yaw_ = args.real("--yaw", 0.0f);
    }
    lift_ = args.real("--lift", 0.0f);

    auto& renderer = get_engine().get_renderer();

    renderer.set_chunk_cull_enabled(args.flag("--chunk-cull"));
    renderer.get_grass_settings().enabled = !args.flag("--no-grass");
    renderer.get_grass_settings().radius_columns = gfx::preset_of(quality_).grass_radius_columns;
    renderer.get_bloom_settings().enabled =
        gfx::preset_of(quality_).bloom && !args.flag("--no-bloom");
    renderer.get_cluster_settings().enabled = !args.flag("--no-clusters");

    if (const auto visible = args.count("--max-visible-lights", 0); visible > 0) {
        renderer.get_max_visible_lights() = visible;
    }
    if (const auto tile = args.count("--cluster-tile", 0); tile > 0) {
        renderer.get_cluster_settings().tile_size = tile;
    }
    if (const auto slices = args.count("--cluster-slices", 0); slices > 0) {
        renderer.get_cluster_settings().slices = slices;
    }
    if (const auto cap = args.count("--cluster-cap", 0); cap > 0) {
        renderer.get_cluster_settings().cap = cap;
    }

    if (const auto wanted = args.text("--debug-view")) {
        const auto found = std::ranges::find(gfx::debug_view_names, *wanted);
        if (found == gfx::debug_view_names.end()) {
            throw std::runtime_error(std::format("unknown debug view '{}'", *wanted));
        }
        renderer.set_debug_view(
            static_cast<gfx::debug_view>(found - gfx::debug_view_names.begin())
        );
    }

    renderer.get_light_cache_settings().enabled = args.text("--light") == "cache";
    if (const auto bricks = args.count("--light-bricks", 0); bricks > 0) {
        renderer.get_light_cache_settings().bricks_per_frame = bricks;
    }

    if (args.text("--sun-elevation")) {
        sun_pinned_ = true;
        const float32 elevation = math::radians(args.real("--sun-elevation", 90.0f));
        const float32 azimuth   = math::radians(args.real("--sun-azimuth", 0.0f));

        renderer.get_directional_light_settings().direction = vec3f{
            -std::cos(elevation) * std::cos(azimuth), -std::sin(elevation),
            -std::cos(elevation) * std::sin(azimuth)
        };
    }

    if (const auto wanted = args.text("--corners")) {
        const auto found = std::ranges::find(gfx::corner_source_names, *wanted);
        if (found == gfx::corner_source_names.end()) {
            throw std::runtime_error(std::format("unknown corner source '{}'", *wanted));
        }
        renderer.get_ambient_settings().corners =
            static_cast<gfx::corner_source>(found - gfx::corner_source_names.begin());
    }

    if (clusters_.wanted()) {
        renderer.set_cluster_readback(clusters_.readback_level());
    }

    auto& window = get_engine().get_window();
    auto& camera = get_engine().get_camera();

    camera_controller_ = std::make_unique<gfx::free_camera_controller>(0.1f, 60.0f);
    camera_controller_->setup(window, camera);

    window.sub<plat::key_press_event>([this](const plat::key_press_event& event) -> bool {
        handle_key_press(event.key);
        return true;
    });

    window.sub<plat::window_close_event>([this](plat::window_close_event&) -> bool {
        get_engine().shutdown();
        return true;
    });

    window.sub<plat::mouse_press_event>([this](const plat::mouse_press_event& event) -> bool {
        if (event.button == plat::mouse::buttons::LEFT &&
            camera_controller_->is_mouse_captured()) {
            apply_tool_();
        }
        return true;
    });

    renderer.set_clear_color(0.4f, 0.6f, 0.9f, 1.0f);
    get_engine().get_debug_tool().set_visible(true);

    setup_world_grid();

    const float32 draw_reach = static_cast<float32>(view_distance_) *
                               static_cast<float32>(ecs::chunk::size) *
                               static_cast<float32>(generator_params_.world_units_per_voxel);

    auto& fog         = renderer.get_fog_settings();
    fog.color         = {0.4f, 0.6f, 0.9f};
    fog.near_distance = 0.6f * draw_reach;
    fog.far_distance  = 0.9f * draw_reach;

    camera.set_far(fog.far_distance);

    if (const float32 view_height = args.real("--orthographic", 0.0f); view_height > 0.0f) {
        camera.set_orthographic(view_height);
    }

    camera.set_rotation(0.0f, 0.0f);

    scene_ = make_scene(*this);
    rig_   = make_camera ? make_camera(*this) : scene_camera_();
}

auto testbed_app::scene_camera_() -> std::unique_ptr<camera_rig> {
    const auto wanted = scene_->default_camera().rig;

    const auto factory = find_camera(wanted);
    if (!factory) {
        throw std::runtime_error(
            std::format("scene '{}' asks for unknown camera '{}'", scene_->name(), wanted)
        );
    }

    return (*factory)(*this);
}

testbed_app::~testbed_app() {
    set_torch_(false);

    if (viewer_.is_valid()) {
        get_engine().get_world().destroy(viewer_);
    }
}

auto testbed_app::world() const -> ecs::world& {
    return get_engine().get_world();
}

auto testbed_app::renderer() const -> gfx::renderer& {
    return get_engine().get_renderer();
}

auto testbed_app::camera() const -> gfx::camera& {
    return get_engine().get_camera();
}

auto testbed_app::is_bench_ready() const -> bool {
    if (bench_ready_) {
        return true;
    }
    if (!camera_placed_) {
        return false;
    }

    bench_ready_ = streaming_settled() && (scene_ == nullptr || scene_->is_ready());
    return bench_ready_;
}

auto testbed_app::render(
    float32 delta_time
) -> void {
    if (camera_placed_ || !rig_->needs_ground()) {
        rig_->drive(aimed_(scene_->default_camera()), delta_time);
    }

    try_place_camera();

    if (!world_ready_ && camera_placed_ && streaming_settled()) {
        world_ready_ = true;
        scene_->on_world_ready();
    }

    scene_->tick(delta_time);
    clusters_.collect(get_engine().get_renderer(), !benching_ || bench_ready_);
    tick_day_night_(delta_time);

    const auto cam_pos = get_engine().get_camera().get_position();

    auto& world_ref     = get_engine().get_world();
    auto& transform_sys = world_ref.system<ecs::transform_system>();
    transform_sys.modify(viewer_).set_position(cam_pos);

    tick_torch_(cam_pos);

    auto& renderer_ref = get_engine().get_renderer();
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{100, 0, 0}, colors::red_8);
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{0, 100, 0}, colors::green_8);
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{0, 0, 100}, colors::blue_8);

    update_hovered_();
    draw_hover_();

    render_ui();
    tick_shot_();
}

auto testbed_app::aimed_(
    camera_hint hint
) const -> camera_hint {
    hint.pitch = pitch_.value_or(hint.pitch);
    hint.yaw   = yaw_.value_or(hint.yaw);
    hint.offset.y += lift_;
    return hint;
}

auto testbed_app::tick_shot_() -> void {
    constexpr int32 settle_frames = 90;
    constexpr log::log_category lc{"testbed"};

    if (!shot_path_ || !world_ready_ || ++shot_frames_ < settle_frames) {
        return;
    }

    auto& renderer = get_engine().get_renderer();
    if (!shot_requested_) {
        shot_requested_ = renderer.request_capture({});
        return;
    }

    const auto frame = renderer.take_capture();
    if (!frame) {
        return;
    }

    if (const auto encoded = gfx::encode_png(*frame)) {
        std::ofstream out{*shot_path_, std::ios::binary};
        out.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
        log::info(lc, "frame written to {}", shot_path_->string());
    } else {
        log::warn(lc, "the frame could not be encoded");
    }
    shot_path_.reset();
    get_engine().shutdown();
}

auto testbed_app::collect_report(gfx::report& out) const -> void {
    out.section("stand")
        .value("scene", scene_->name())
        .value("camera", rig_->name())
        .value("quality", gfx::name_of(quality_))
        .value("view_distance", static_cast<uint64>(view_distance_))
        .value("lod_distance", lod_distance_);

    scene_->collect_report(out);
    clusters_.collect_report(out);
}

}  // namespace vw::testbed
