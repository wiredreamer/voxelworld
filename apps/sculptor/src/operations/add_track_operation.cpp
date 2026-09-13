module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_track_operation::add_track_operation(
    engine_type& engine, app_state& state, const add_track_params& params
)
    : base_operation(), engine_(&engine), state_(&state), params_(params) {}

auto add_track_operation::execute() -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(params_.clip_name);
    if (!clip) {
        return;
    }

    asset::animation_track track(params_.track_name);

    if (params_.property.has_value() && params_.keyframe.has_value()) {
        const auto prop = params_.property.value();
        const auto& kf  = params_.keyframe.value();

        if (prop == asset::animation_property::rotation) {
            auto channel = asset::make_animation_channel<asset::animation_property::rotation>();
            channel.add(std::get<asset::keyframe_quat>(kf));
            track.add<asset::animation_property::rotation>(std::move(channel));
        } else if (prop == asset::animation_property::position) {
            auto channel = asset::make_animation_channel<asset::animation_property::position>();
            channel.add(std::get<asset::keyframe_vec3f>(kf));
            track.add<asset::animation_property::position>(std::move(channel));
        } else if (prop == asset::animation_property::scale) {
            auto channel = asset::make_animation_channel<asset::animation_property::scale>();
            channel.add(std::get<asset::keyframe_vec3f>(kf));
            track.add<asset::animation_property::scale>(std::move(channel));
        } else if (prop == asset::animation_property::origin) {
            auto channel = asset::make_animation_channel<asset::animation_property::origin>();
            channel.add(std::get<asset::keyframe_vec3f>(kf));
            track.add<asset::animation_property::origin>(std::move(channel));
        }
    }

    clip->add_track(std::move(track));

    if (state_->scene.name_to_entity.contains(params_.track_name)) {
        const auto ent = state_->scene.name_to_entity[params_.track_name];
        auto& world    = engine_->get_world();
        if (!world.has<ecs::animation_target_component>(ent)) {
            added_target_component_ = true;
            world.modify(ent).with<ecs::animation_target_component>();
            auto target_mod = world.system<ecs::animation_system>().modify_target(ent);
            target_mod.set_target_name(params_.track_name);
            if (world.has<ecs::transform_component>(ent)) {
                target_mod.set_rest_transform(
                    world.get<ecs::transform_component>(ent).get_transform()
                );
            }
        }
    }

    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

auto add_track_operation::undo() -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(params_.clip_name);
    if (!clip) {
        return;
    }

    clip->remove_track(params_.track_name);
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
