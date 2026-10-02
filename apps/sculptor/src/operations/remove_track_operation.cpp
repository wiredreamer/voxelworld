module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

remove_track_operation::remove_track_operation(
    engine_type& eng, app_state& state, remove_track_params params
)
    : engine_(&eng), state_(&state), params_(std::move(params)) {}

auto remove_track_operation::execute() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    auto clip      = registry.get(params_.clip_name);
    if (!clip) {
        return;
    }

    auto* track = clip->get_track(params_.track_name);
    if (track) {
        saved_track_ = *track;
    }

    clip->remove_track(params_.track_name);
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;

    if (state_->anim.selected_track_name == params_.track_name) {
        state_->anim.selected_track_name.clear();
        state_->anim.selected_keyframe_id = asset::invalid_keyframe_id;
    }
    state_->anim.expanded_tracks.erase(params_.track_name);
}

auto remove_track_operation::undo() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    auto clip      = registry.get(params_.clip_name);
    if (!clip || !saved_track_) {
        return;
    }
    clip->add_track(*saved_track_);
    state_->anim.need_apply_pose                  = true;
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
