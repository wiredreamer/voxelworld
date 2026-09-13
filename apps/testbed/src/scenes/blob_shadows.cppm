export module vw.testbed:scenes.blob_shadows;

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

// Кольцо тел вокруг камеры, каждое качается на свою высоту, — чтобы смотреть на
// пятно под ними. Одно из них намеренно не отрывается от земли: судить, бледнеет
// ли тень, когда её хозяин поднимается, по памяти невозможно, если в том же
// кадре нет стоящего на земле.
class blob_shadows_scene final : public scene {
public:
    blob_shadows_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "blob-shadows";
    }

    auto tick(float32 delta_time) -> void override;

    // Медленно по кругу, чтобы всё кольцо прошло через вид, и круче вниз, чем
    // где-либо ещё: смотрят здесь на пятно под телом, а не на тело.
    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.rig = "spin", .pitch = -25.0f, .degrees_per_frame = 0.0625f};
    }

    // Кольцо встаёт колонка за колонкой, поэтому последнее тело может встать
    // позже последней колонки. Мерить кольцо, которое ещё достраивается, значит
    // считать пятна, которых пока нет.
    [[nodiscard]] auto is_ready() const -> bool override {
        return seeded_ && pending_.empty();
    }

    auto ui() -> void override;

private:
    // Двадцать четыре вокселя — пара сотен мировых единиц: близко, чтобы пятно
    // на земле стоило рассматривать, и далеко, чтобы кольцо целиком помещалось
    // в кадр из его середины.
    static constexpr int32 ring = 24;

    // Поднятая косинусоида от номера кадра: начинается на земле, возвращается
    // на неё и никогда не уходит ниже.
    struct bob {
        ecs::entity ent;
        float32 x         = 0.0f;
        float32 z         = 0.0f;
        float32 ground    = 0.0f;
        float32 amplitude = 0.0f;
        float32 speed     = 0.0f;
        float32 phase     = 0.0f;
    };

    auto spawn_() -> void;

    // Тел в кольце: --bodies.
    int32 bodies_asked_ = 8;

    bool seeded_      = false;
    uint32 bob_frame_ = 0;
    std::vector<bob> bodies_;

    // Индексы кольца, которые ещё ждут земли под собой, и одна модель на всех.
    std::vector<int32> pending_;
    std::shared_ptr<asset::model> model_;
};

}  // namespace vw::testbed
