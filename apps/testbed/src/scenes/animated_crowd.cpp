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

animated_crowd_scene::animated_crowd_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}, size_{args.count("--bodies", 50)} {}

auto animated_crowd_scene::on_world_ready() -> void {
    if (spawned_ || size_ == 0) {
        return;
    }

    spawn_();
    spawned_ = true;
}

auto animated_crowd_scene::tick(float32) -> void {
    if (bodies_.empty()) {
        return;
    }
    if (settle_frames_ < settle_target) {
        ++settle_frames_;
    }
}

auto animated_crowd_scene::is_ready() const -> bool {
    if (!spawned_) {
        return false;
    }

    return settle_frames_ >= settle_target || grounded_() == bodies_.size();
}

auto animated_crowd_scene::grounded_() const -> std::size_t {
    auto& world = stand().world();

    return static_cast<std::size_t>(std::ranges::count_if(bodies_, [&world](const body& b) {
        return world.has<ecs::rigid_body_component>(b.ent) &&
            world.get<ecs::rigid_body_component>(b.ent).is_grounded();
    }));
}

auto animated_crowd_scene::drift_() const -> float32 {
    auto& world = stand().world();

    float32 worst = 0.0f;
    for (const body& b : bodies_) {
        if (!world.has<ecs::transform_component>(b.ent)) {
            continue;
        }

        const auto at = world.get<ecs::transform_component>(b.ent).get_position();
        worst = std::max(worst, math::length(vec2f{at.x, at.z} - b.home));
    }

    return worst;
}

auto animated_crowd_scene::ground_at_(
    float32 x, float32 z
) const -> float32 {
    const auto scale = static_cast<float32>(stand().voxel_scale());
    const auto& grid = stand().grid();

    std::optional<int32> top;
    for (const float32 dz : {-collider_half_width, collider_half_width}) {
        for (const float32 dx : {-collider_half_width, collider_half_width}) {
            const auto column = grid.get_surface_y(
                static_cast<int32>(std::floor((x + dx) / scale)),
                static_cast<int32>(std::floor((z + dz) / scale))
            );
            if (column && (!top || *column > *top)) {
                top = column;
            }
        }
    }

    return top ? static_cast<float32>(*top + 1) * scale : stand().altitude();
}

auto animated_crowd_scene::make_clip_(
    ecs::world& world
) -> std::shared_ptr<asset::animation_clip> {
    auto& clips = world.resource<asset::animation_clip_registry>();
    auto clip   = clips.create("crowd_wave");

    for (const auto& part : parts) {
        asset::animation_track track{std::string{part.target}, 60.0f};

        asset::animation_channel<vec3f> channel{asset::animation_property::position};
        channel.add(asset::keyframe_vec3f{0.0f, part.rest});
        channel.add(asset::keyframe_vec3f{part.peak, part.rest + vec3f{0.0f, part.lift, 0.0f}});
        channel.add(asset::keyframe_vec3f{wave_seconds, part.rest});
        track.add<asset::animation_property::position>(std::move(channel));

        clip->add_track(std::move(track));
    }

    return clip;
}

auto animated_crowd_scene::spawn_() -> void {
    auto& world    = stand().world();
    auto& registry = world.resource<asset::model_registry>();

    std::array<std::shared_ptr<asset::model>, parts.size()> models;
    for (std::size_t part = 0; part < parts.size(); ++part) {
        if (registry.has(parts[part].model)) {
            models[part] = registry.get(parts[part].model);
            continue;
        }

        models[part] = registry.create(
            parts[part].model, parts[part].fill.category(), parts[part].size
        );
        models[part]->fill(parts[part].fill);
    }

    const auto clip = make_clip_(world);

    const auto side =
        static_cast<int32>(std::ceil(std::sqrt(static_cast<float32>(size_))));
    const float32 origin = -0.5f * static_cast<float32>(side - 1) * spacing;

    auto& transform_sys = world.system<ecs::transform_system>();
    auto& hierarchy_sys = world.system<ecs::hierarchy_system>();
    auto& physics_sys   = world.system<ecs::physics_system>();
    auto& model_sys     = world.system<ecs::model_system>();
    auto& anim_sys      = world.system<ecs::animation_system>();

    bodies_.reserve(size_);

    for (uint32 i = 0; i < size_; ++i) {
        const auto col = static_cast<int32>(i) % side;
        const auto row = static_cast<int32>(i) / side;

        const float32 x = origin + (static_cast<float32>(col) * spacing);
        const float32 z = origin + (static_cast<float32>(row) * spacing);

        const auto root = world.create()
                              .with<ecs::hierarchy_component>()
                              .with<ecs::transform_component>()
                              .with<ecs::spatial_component>()
                              .with<ecs::rigid_body_component>()
                              .with<ecs::box_collider_component>()
                              .with<ecs::animation_player_component>()
                              .with(ecs::blob_shadow_component{8.0f, 20.0f, 0.55f})
                              .get_entity();

        transform_sys.modify(root).set_position({x, ground_at_(x, z) + drop_height, z});
        physics_sys.modify_collider(root)
            .set_extents(collider_extents)
            .set_offset({0.0f, collider_extents.y * 0.5f, 0.0f});
        world.system<ecs::spatial_system>().modify(root).set_layer(
            ecs::spatial_layer::character
        );

        for (std::size_t part = 0; part < parts.size(); ++part) {
            const auto ent = world.create()
                                 .with<ecs::hierarchy_component>()
                                 .with<ecs::transform_component>()
                                 .with<ecs::spatial_component>()
                                 .with<ecs::model_component>()
                                 .with<ecs::animation_target_component>()
                                 .get_entity();

            hierarchy_sys.modify(ent).set_parent(root);

            transform rest;
            rest.set_position(parts[part].rest);
            transform_sys.modify(ent).set_transform(rest);
            model_sys.modify(ent).set_model(models[part]);

            const auto target = anim_sys.modify_target(ent);
            target.set_target_name(std::string{parts[part].target});
            target.set_rest_transform(rest);
        }

        auto player = anim_sys.modify_player(root);
        player.add_layer(0);
        player.layer(0).blend_to(clip);
        player.layer(0).set_loop_mode(asset::animation_loop_mode::loop);
        player.layer(0).play();

        bodies_.push_back(body{.ent = root, .home = {x, z}});
    }

    log::info(
        "animated-crowd: {} bodies of {} parts each, dropped {} units onto their own columns",
        bodies_.size(), parts.size(), drop_height
    );
}

auto animated_crowd_scene::collect_report(
    gfx::report& out
) const -> void {
    out.section("crowd")
        .value("bodies", static_cast<uint64>(bodies_.size()))
        .value("entities", static_cast<uint64>(bodies_.size() * (1 + parts.size())))
        .value("grounded", static_cast<uint64>(grounded_()))
        .value("settle_frames", static_cast<uint64>(settle_frames_))
        .value("drift", static_cast<float64>(drift_()), 2);
}

auto animated_crowd_scene::ui() -> void {
    ImGui::Text("animated-crowd: %u bodies, %zu grounded, drift %.2f, settled %u/%u", size_,
                grounded_(), static_cast<float64>(drift_()), settle_frames_, settle_target);
}

}  // namespace vw::testbed
