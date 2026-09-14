module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

keyframe_service::keyframe_service(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto keyframe_service::add_keyframe() -> void {
    if (state_->anim.selected_clip_name.empty() || state_->scene.selected_name.empty()) {
        return;
    }

    auto& world    = engine_->get_world();
    auto& clip_reg = world.resource<asset::animation_clip_registry>();
    auto clip      = clip_reg.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    auto entity_name = state_->scene.selected_name;
    auto ent         = state_->scene.name_to_entity[entity_name];
    auto prop        = state_->anim.selected_property;
    float32 time     = state_->anim.timeline_cursor;

    if (!world.has<ecs::transform_component>(ent)) {
        return;
    }

    auto& tc = world.get<ecs::transform_component>(ent);

    keyframe_value kf_val;
    if (prop == asset::animation_property::rotation) {
        kf_val = asset::keyframe_quat(time, tc.get_rotation());
    } else if (prop == asset::animation_property::position) {
        kf_val = asset::keyframe_vec3f(time, tc.get_position());
    } else {
        kf_val = asset::keyframe_vec3f(time, tc.get_scale());
    }

    if (!clip->has_track(entity_name)) {
        add_track_params params = {
            .clip_name  = state_->anim.selected_clip_name,
            .track_name = entity_name,
            .property   = prop,
            .keyframe   = kf_val,
        };
        auto op = std::make_unique<add_track_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));
    } else {
        add_keyframe_params params = {
            .clip_name  = state_->anim.selected_clip_name,
            .track_name = entity_name,
            .property   = prop,
            .keyframe   = kf_val,
        };
        auto op = std::make_unique<add_keyframe_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));
    }
}

auto keyframe_service::delete_keyframe() -> void {
    if (state_->anim.selected_keyframe_id == asset::invalid_keyframe_id ||
        state_->anim.selected_clip_name.empty() || state_->anim.selected_track_name.empty()) {
        return;
    }

    auto& world          = engine_->get_world();
    const auto& clip_reg = world.resource<asset::animation_clip_registry>();
    const auto clip      = clip_reg.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    const auto* track = clip->get_track(state_->anim.selected_track_name);
    if (track == nullptr) {
        return;
    }

    const auto* ch = track->get_channel(state_->anim.selected_property);
    if (ch == nullptr) {
        return;
    }

    std::visit(
        [&](const auto& channel) {
            for (const auto& kf : channel.get_keyframes()) {
                if (kf.id() == state_->anim.selected_keyframe_id) {
                    remove_keyframe_params params;
                    params.clip_name  = state_->anim.selected_clip_name;
                    params.track_name = state_->anim.selected_track_name;
                    params.property   = state_->anim.selected_property;
                    params.keyframe   = keyframe_value(kf);
                    auto op =
                        std::make_unique<remove_keyframe_operation>(*engine_, *state_, params);
                    op_manager_->execute(std::move(op));
                    return;
                }
            }
        },
        *ch
    );
}

}  // namespace vw::sculptor
