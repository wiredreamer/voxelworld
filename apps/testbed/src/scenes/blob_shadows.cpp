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

    const auto scale = static_cast<float32>(stand().world_units_per_voxel());
    const auto count = std::max(bodies_asked_, 1);

    if (!seeded_) {
        model_ = registry.create("blob_body", 16, 40, 16);
        model_->fill(voxels::red[8]);

        pending_.reserve(static_cast<std::size_t>(count));
        for (int32 i = 0; i < count; ++i) {
            pending_.push_back(i);
        }

        seeded_ = true;
    }

    std::size_t keep = 0;

    for (std::size_t at = 0; at < pending_.size(); ++at) {
        const int32 i = pending_[at];

        const float32 angle =
            (static_cast<float32>(i) / static_cast<float32>(count)) * 2.0f * math::pi;

        const auto vx =
            static_cast<int32>(std::lround(static_cast<float32>(ring) * std::cos(angle)));
        const auto vz =
            static_cast<int32>(std::lround(static_cast<float32>(ring) * std::sin(angle)));

        std::optional<int32> surface;
        for (int32 dz = 0; dz <= 1; ++dz) {
            for (int32 dx = 0; dx <= 1; ++dx) {
                const auto column = stand().grid().get_surface_voxel_y(vx + dx, vz + dz);
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

            .amplitude =
                (static_cast<float32>(i) / static_cast<float32>(count)) * 1.3f * 48.0f,

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

auto blob_shadows_scene::tick(float32) -> void {
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
