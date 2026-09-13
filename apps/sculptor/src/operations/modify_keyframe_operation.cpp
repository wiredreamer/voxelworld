module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

modify_keyframe_operation::modify_keyframe_operation(
    engine_type& engine, app_state& state, const modify_keyframe_params& params
)
    : base_operation(), engine_(&engine), state_(&state), params_(params) {}

auto modify_keyframe_operation::execute() -> void {
    apply(params_.new_keyframe);
}

auto modify_keyframe_operation::undo() -> void {
    apply(params_.old_keyframe);
}

auto modify_keyframe_operation::apply(
    const keyframe_value& replacement
) const -> void {
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
        return;
    }

    const uint32 id =
        std::visit([](const auto& kf) -> uint32 { return kf.id(); }, params_.old_keyframe);

    if (params_.property == asset::animation_property::rotation) {
        auto& channel = std::get<asset::animation_channel<quat>>(*channel_var);
        channel.replace(id, std::get<asset::keyframe_quat>(replacement));
    } else {
        auto& channel = std::get<asset::animation_channel<vec3f>>(*channel_var);
        channel.replace(id, std::get<asset::keyframe_vec3f>(replacement));
    }

    track->mark_dirty();
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
