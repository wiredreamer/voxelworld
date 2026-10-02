module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_clip_tracks_operation::set_clip_tracks_operation(
    engine_type& eng, app_state& st, set_clip_tracks_params params
)
    : engine_(&eng), state_(&st), params_(std::move(params)) {}

auto set_clip_tracks_operation::execute() -> void {
    const auto clip =
        engine_->get_world().resource<asset::animation_clip_registry>().get(params_.clip_name);
    if (!clip) {
        return;
    }

    previous_ = clip->get_tracks();
    apply_(params_.tracks);
}

auto set_clip_tracks_operation::undo() -> void {
    apply_(previous_);
}

auto set_clip_tracks_operation::apply_(const std::vector<asset::animation_track>& tracks) -> void {
    const auto clip =
        engine_->get_world().resource<asset::animation_clip_registry>().get(params_.clip_name);
    if (!clip) {
        return;
    }

    std::vector<std::string> current;
    for (const asset::animation_track& track : clip->get_tracks()) {
        current.push_back(track.get_target_name());
    }
    for (const std::string& target : current) {
        clip->remove_track(target);
    }
    for (const asset::animation_track& track : tracks) {
        clip->add_track(track);
    }

    auto& anim = state_->anim;

    anim.selected_keyframe_id = asset::invalid_keyframe_id;
    if (!clip->has_track(anim.selected_track_name)) {
        anim.selected_track_name.clear();
    }
    std::erase_if(anim.expanded_tracks, [&clip](const auto& target) {
        return !clip->has_track(target);
    });

    anim.need_apply_pose                  = true;
    anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
