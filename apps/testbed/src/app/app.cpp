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
    // Порядок повторяет объявление полей: иначе список врёт о том, что
    // произойдёт на самом деле, и -Wreorder-ctor это ловит.
    : app{eng}
    // --sun: солнце ходит и в замерном прогоне. По умолчанию нет — свет,
    // который поворачивается, обнуляет каждый каскад, и прогон мерил бы солнце,
    // а не то, что проверяют.
    , sun_in_bench_{args.flag("--sun")}
    , benching_{args.flag("--bench")}
    , clusters_{args.flag("--cluster-stats"), args.count("--verify-lights", 0)} {
    auto& renderer = get_engine().get_renderer();

    renderer.set_chunk_cull_enabled(args.flag("--chunk-cull"));
    renderer.get_cluster_settings().enabled = !args.flag("--no-clusters");

    // Ноль оставляет движку его умолчание, для всех четырёх.
    if (const auto visible = args.count("--max-visible-lights", 0); visible > 0) {
        renderer.get_max_visible_lights() = visible;
    }
    if (const auto tile = args.count("--cluster-tile", 0); tile > 0) {
        renderer.get_cluster_settings().tile_size = tile;
    }
    if (const auto slices = args.count("--cluster-slices", 0); slices > 0) {
        // Один срез — это ровно плоские тайлы, единственный честный способ
        // назвать цену резки по глубине.
        renderer.get_cluster_settings().slices = slices;
    }
    if (const auto cap = args.count("--cluster-cap", 0); cap > 0) {
        renderer.get_cluster_settings().cap = cap;
    }

    // Проверка подразумевает списки; одни счётчики избавляют от мегабайтов,
    // которых списки стоят каждый кадр.
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

    // Только пока курсор захвачен. Без захвата левая кнопка принадлежит панели,
    // и клик по ползунку не должен заодно копать яму в том, что за ним.
    window.sub<plat::mouse_press_event>([this](const plat::mouse_press_event& event) -> bool {
        if (event.button == plat::mouse::buttons::LEFT &&
            camera_controller_->is_mouse_captured()) {
            apply_tool_();
        }
        return true;
    });

    renderer.set_clear_color(0.4f, 0.6f, 0.9f, 1.0f);
    get_engine().get_debug_tool().set_visible(true);

    auto& fog         = renderer.get_fog_settings();
    fog.color         = {0.4f, 0.6f, 0.9f};
    fog.near_distance = 6 * 64 * 8;
    fog.far_distance  = 9 * 64 * 8;

    // Всё за туманом — сплошной цвет тумана, поэтому рисовать его чистая трата;
    // дальняя плоскость и есть то, чем отсев по пирамиде это отбрасывает.
    camera.set_far(fog.far_distance);

    setup_world_grid();
    camera.set_rotation(0.0f, 0.0f);

    viewer_ = get_engine().get_world().create().with<ecs::world_view_component>().get_entity();

    // Сцена строится последней: её конструктор вправе спрашивать стенд про мир,
    // камеру и рельеф, а к этому моменту всё это уже стоит.
    scene_ = make_scene(*this);
    rig_   = make_camera ? make_camera(*this) : scene_camera_();
}

// Путь, который сцена назначила себе сама. Имени, которого нет в таблице, здесь
// взяться неоткуда — оно приходит из кода сцены, а не из командной строки,
// поэтому это нарушение инварианта, а не ошибка пользователя.
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

// Защёлка: она сторожит только начальную загрузку. Стоит облёту начаться, как
// стриминг больше не успокаивается никогда, а его цена — ровно то, что эта
// сцена и меряет.
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
        rig_->drive(scene_->default_camera(), delta_time);
    }

    try_place_camera();

    // Мир вокруг догрузился — сцене можно расставлять своё содержимое: до этого
    // момента под ним может не быть земли.
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
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{100, 0, 0}, colors::red_4);
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{0, 100, 0}, colors::green_4);
    renderer_ref.draw_line(vec3f{0, 0, 0}, vec3f{0, 0, 100}, colors::blue_4);

    update_hovered_();
    draw_hover_();

    render_ui();
}

auto testbed_app::collect_report(gfx::report& out) const -> void {
    // Что именно мерили, прямо в отчёте: сцена и путь теперь выбираются
    // порознь, и по одному имени файла уже не сказать, чей это прогон.
    out.section("stand").value("scene", scene_->name()).value("camera", rig_->name());

    scene_->collect_report(out);
    clusters_.collect_report(out);
}

}  // namespace vw::testbed
