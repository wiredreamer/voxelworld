export module vw.testbed:scenes.animated_crowd;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :app;
import :args;
import :camera;
import :scene;

export namespace vw::testbed {

// Толпа анимированных физических тел: сцена, о которой продукт и говорит, и
// единственная, где работа CPU на сущность вообще видна.
//
// Тела сбрасываются в воздухе и должны приземлиться до того, как их начнут
// мерить: пойманные в падении, они кладут в каждый прогон разное количество
// работы, и разброс топит то, ради чего сцена существует.
class animated_crowd_scene final : public scene {
public:
    animated_crowd_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "animated-crowd";
    }

    auto tick(float32 delta_time) -> void override;

    // Единственная сцена, которую смотрят со стороны: толпу видно только тогда,
    // когда камера отошла от неё и развернулась.
    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.offset = {0.0f, 60.0f, 260.0f}, .pitch = -15.0f, .yaw = 180.0f};
    }
    auto on_world_ready() -> void override;

    [[nodiscard]] auto is_ready() const -> bool override;

    auto collect_report(gfx::report& out) const -> void override;
    auto ui() -> void override;

private:
    // Предохранитель, а не срок: готовность даёт заземление всех тел, а этот
    // счётчик — только то, за сколько кадров сцена сдаётся и начинает мерить
    // толпу, в которой кто-то так и не встал. Что таких было, видно в отчёте.
    static constexpr uint32 settle_target = 400;

    // Коробка физики. Смещена на половину высоты вверх, поэтому начало
    // координат корня — точка между ступнями, а не середина тела: по ней стоит
    // пятно тени, от неё же отмеряются части.
    static constexpr vec3f collider_extents{12.0f, 24.0f, 12.0f};
    static constexpr float32 collider_half_width = 6.0f;

    // Роняют с двух вокселей рельефа: посадка на неровную землю оставляет тела
    // частично вкопанными, а выталкивание хуже падения. Выше не нужно — падение
    // длиннее только разгоняет тело и загоняет его в землю глубже.
    static constexpr float32 drop_height = 16.0f;

    static constexpr float32 wave_seconds = 1.0f;
    static constexpr float32 spacing      = 40.0f;

    // Часть фигуры. Воксельная модель растёт от своего угла, поэтому rest — это
    // смещение угла от ступней, и центрировать по x и z приходится вручную,
    // вычитанием половины размера.
    //
    // Между частями всюду зазор в единицу. У соприкасающихся коробок грани
    // ложатся плоскость в плоскость, глубины у них выходят побитово равными, и
    // кто из двух виден — решает порядок отрисовки; у ползающих друг вдоль
    // друга частей он меняется, и это читается дрожью.
    //
    // lift и peak — гребень подъёма и его момент внутри цикла. Моменты у частей
    // разные: тем и отличается волна от четырёх подпрыгиваний разом.
    struct body_part {
        std::string_view target;
        std::string_view model;
        vec3i size;
        vec3f rest;
        voxel fill;
        float32 lift;
        float32 peak;
    };

    static constexpr std::array<body_part, 4> parts{{
        {.target = "body", .model = "crowd_body", .size = {6, 12, 4},
         .rest = {-3.0f, 0.0f, -2.0f}, .fill = voxels::palette::blue[2], .lift = 1.0f, .peak = 0.30f},
        {.target = "head", .model = "crowd_head", .size = {6, 6, 6},
         .rest = {-3.0f, 13.0f, -3.0f}, .fill = voxels::world::dirt[2], .lift = 2.0f, .peak = 0.45f},
        // Обе ладони делят одну модель: их две штуки на тело, и вторая копия
        // тех же двухсот вокселей ничего не показывает.
        {.target = "hand_left", .model = "crowd_hand", .size = {3, 8, 3},
         .rest = {-7.0f, 2.0f, -1.5f}, .fill = voxels::world::grass[2], .lift = 6.0f, .peak = 0.15f},
        {.target = "hand_right", .model = "crowd_hand", .size = {3, 8, 3},
         .rest = {4.0f, 2.0f, -1.5f}, .fill = voxels::world::grass[2], .lift = 6.0f, .peak = 0.60f},
    }};

    // Тело и место, куда его уронили. Тела здесь должны стоять, поэтому снос от
    // своей клетки — число, которое нечему делать большим: рельеф, толкающий
    // коробку вбок, и соседи, расталкивающие друг друга, видны только по нему.
    struct body {
        ecs::entity ent;
        vec2f home;
    };

    [[nodiscard]] static auto make_clip_(ecs::world& world)
        -> std::shared_ptr<asset::animation_clip>;

    // Верх колонки под телом, в мировых единицах.
    [[nodiscard]] auto ground_at_(float32 x, float32 z) const -> float32;
    [[nodiscard]] auto grounded_() const -> std::size_t;
    [[nodiscard]] auto drift_() const -> float32;

    auto spawn_() -> void;

    // Тел: --bodies. Полсотни по умолчанию — столько эта сцена и мерила,
    // когда размер задавался снаружи.
    uint32 size_ = 50;

    std::vector<body> bodies_;
    uint32 settle_frames_ = 0;
    bool spawned_         = false;
};

}  // namespace vw::testbed
