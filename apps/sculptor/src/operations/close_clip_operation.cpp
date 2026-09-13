module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

close_clip_operation::close_clip_operation(
    engine_type& engine, app_state& state, const close_clip_params& params
)
    : base_operation(), engine_(&engine), state_(&state), params_(params) {}

auto close_clip_operation::execute() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    saved_clip_    = registry.get(params_.name);
    registry.remove(params_.name);

    if (state_->anim.selected_clip_name == params_.name) {
        state_->anim.selected_clip_name.clear();
        state_->anim.selected_track_name.clear();
        state_->anim.selected_keyframe_id = asset::invalid_keyframe_id;
    }
    state_->anim.unsaved_clips.erase(params_.name);
}

auto close_clip_operation::undo() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    registry.add(params_.name, saved_clip_);
    state_->anim.selected_clip_name = params_.name;
}

}  // namespace vw::sculptor
