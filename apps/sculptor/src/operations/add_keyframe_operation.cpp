module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_keyframe_operation::add_keyframe_operation(
    engine_type& engine, app_state& state, const add_keyframe_params& params
)
    : engine_(&engine), state_(&state), params_(params) {}

auto add_keyframe_operation::execute() -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(params_.clip_name);
    if (!clip) {
        return;
    }

    auto* track = clip->get_track_mut(params_.track_name);
    if (!track) {
        return;
    }

    auto* channel_var = track->get_channel_mut(params_.property);
    if (!channel_var) {
        if (params_.property == asset::animation_property::rotation) {
            auto channel = asset::make_animation_channel<asset::animation_property::rotation>();
            channel.add(std::get<asset::keyframe_quat>(params_.keyframe));
            track->add<asset::animation_property::rotation>(std::move(channel));
        } else if (params_.property == asset::animation_property::position) {
            auto channel = asset::make_animation_channel<asset::animation_property::position>();
            channel.add(std::get<asset::keyframe_vec3f>(params_.keyframe));
            track->add<asset::animation_property::position>(std::move(channel));
        } else if (params_.property == asset::animation_property::scale) {
            auto channel = asset::make_animation_channel<asset::animation_property::scale>();
            channel.add(std::get<asset::keyframe_vec3f>(params_.keyframe));
            track->add<asset::animation_property::scale>(std::move(channel));
        } else if (params_.property == asset::animation_property::origin) {
            auto channel = asset::make_animation_channel<asset::animation_property::origin>();
            channel.add(std::get<asset::keyframe_vec3f>(params_.keyframe));
            track->add<asset::animation_property::origin>(std::move(channel));
        }
        created_channel_ = true;
    } else {
        if (params_.property == asset::animation_property::rotation) {
            auto& channel = std::get<asset::animation_channel<quat>>(*channel_var);
            channel.add(std::get<asset::keyframe_quat>(params_.keyframe));
        } else {
            auto& channel = std::get<asset::animation_channel<vec3f>>(*channel_var);
            channel.add(std::get<asset::keyframe_vec3f>(params_.keyframe));
        }
        created_channel_ = false;
    }

    track->mark_dirty();
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

auto add_keyframe_operation::undo() -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(params_.clip_name);
    if (!clip) {
        return;
    }

    auto* track = clip->get_track_mut(params_.track_name);
    if (!track) {
        return;
    }

    if (created_channel_) {
        track->remove_channel(params_.property);
    } else {
        auto* channel_var = track->get_channel_mut(params_.property);
        if (!channel_var) {
            return;
        }

        uint32 remove_id =
            std::visit([](const auto& kf) -> uint32 { return kf.id(); }, params_.keyframe);

        std::visit([remove_id](auto& channel) { channel.remove(remove_id); }, *channel_var);
    }

    track->mark_dirty();
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
