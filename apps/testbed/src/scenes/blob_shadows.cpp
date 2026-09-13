module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::testbed {

blob_shadows_scene::blob_shadows_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}, bodies_asked_{args.integer("--bodies", 8)} {}

auto blob_shadows_scene::spawn_() -> void {
    if (seeded_ && pending_.empty()) {
        return;
    }

    auto& world         = stand().world();
    auto& registry      = world.resource<asset::model_registry>();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& model_sys     = world.system<ecs::model_system>();

    const auto scale = static_cast<float32>(stand().voxel_scale());
    const auto count = std::max(bodies_asked_, 1);

    if (!seeded_) {
        // Одна модель на всех. Воксель модели — одна мировая единица, воксель
        // рельефа — восемь, поэтому тело размером с человека это шестнадцать
        // поперёк и сорок в высоту; построенное по числам рельефа, оно стоит в
        // один воксель и читается крапинкой — с чего эта сцена и начиналась.
        model_ = registry.create("blob_body", blocks::creature::category, 16, 40, 16);
        model_->fill(voxel{blocks::creature::cloth_red[2]});

        pending_.reserve(static_cast<std::size_t>(count));
        for (int32 i = 0; i < count; ++i) {
            pending_.push_back(i);
        }

        seeded_ = true;
    }

    // Тело, чьи колонки ещё не приехали, ждёт их, а не выбрасывается. Именно
    // пропуск таких заставлял сцену ждать весь мир: кольцо вставало одним
    // проходом, и первый его прогон поставил пять тел из восьми. Все числа ниже
    // читают индекс кольца, а не порядок появления земли, поэтому кольцо
    // получается одно и то же в любом случае.
    std::size_t keep = 0;

    for (std::size_t at = 0; at < pending_.size(); ++at) {
        const int32 i = pending_[at];

        const float32 angle =
            (static_cast<float32>(i) / static_cast<float32>(count)) * 2.0f * math::pi;

        const auto vx =
            static_cast<int32>(std::lround(static_cast<float32>(ring) * std::cos(angle)));
        const auto vz =
            static_cast<int32>(std::lround(static_cast<float32>(ring) * std::sin(angle)));

        // Тело шестнадцать единиц поперёк, а воксель рельефа восемь, поэтому
        // стоит оно на четырёх колонках, а не на одной. Посаженное по той, над
        // которой его мерили, оно на остальных трёх может оказаться на воксель
        // ниже и уйти в землю на четверть — плохая опора для стенда, судящего о
        // тенях.
        std::optional<int32> surface;
        for (int32 dz = 0; dz <= 1; ++dz) {
            for (int32 dx = 0; dx <= 1; ++dx) {
                const auto column = stand().grid().get_surface_y(vx + dx, vz + dz);
                if (column && (!surface || *column > *surface)) {
                    surface = column;
                }
            }
        }

        if (!surface) {
            pending_[keep++] = i;
            continue;
        }

        const auto ent = world.create()
                             .with<ecs::transform_component>()
                             .with<ecs::spatial_component>()
                             .with<ecs::model_component>()
                             // Диск чуть шире тела, а тело шестнадцать поперёк,
                             // то есть восемь от середины.
                             .with(ecs::blob_shadow_component{20.0f, 48.0f, 0.6f})
                             .get_entity();

        model_sys.modify(ent).set_model(model_);

        const float32 ground = static_cast<float32>(*surface + 1) * scale;

        transform_sys.modify(ent).set_position(
            {static_cast<float32>(vx) * scale, ground, static_cast<float32>(vz) * scale}
        );

        bodies_.push_back(bob{
            .ent    = ent,
            .x      = static_cast<float32>(vx) * scale,
            .z      = static_cast<float32>(vz) * scale,
            .ground = ground,

            // От нуля до чуть больше высоты падения, вразброс по кольцу. Первое
            // не отрывается от земли вовсе и служит контролем, последнее уходит
            // заметно выше, поэтому и самое широкое пятно, и самое тугое стоят
            // в одном кадре.
            .amplitude =
                (static_cast<float32>(i) / static_cast<float32>(count)) * 1.3f * 48.0f,

            // Разные скорости, иначе они поднимаются и опускаются как одно, и
            // кадр показывает всегда одну высоту.
            .speed = 0.012f + (0.004f * static_cast<float32>(i % 4)),
            .phase = static_cast<float32>(i) * 0.9f,
        });
    }

    pending_.resize(keep);

    if (pending_.empty()) {
        log::info(
            "blob-shadows: {} bodies of {} asked on a ring of {} voxels", bodies_.size(), bodies_asked_,
            ring
        );
    }
}

auto blob_shadows_scene::tick(float32 /*delta_time*/) -> void {
    spawn_();

    if (bodies_.empty()) {
        return;
    }

    auto& transform_sys = stand().world().system<ecs::transform_system>();
    const auto frame    = static_cast<float32>(bob_frame_++);

    for (const bob& b : bodies_) {
        const float32 rise =
            b.amplitude * 0.5f * (1.0f - std::cos((frame * b.speed) + b.phase));

        transform_sys.modify(b.ent).set_position({b.x, b.ground + rise, b.z});
    }
}

auto blob_shadows_scene::ui() -> void {
    ImGui::Text("blob-shadows: %zu bodies of %d asked", bodies_.size(), bodies_asked_);
}

}  // namespace vw::testbed
