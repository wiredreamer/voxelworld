module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

create_clip_operation::create_clip_operation(
    engine_type& engine, app_state& state, const create_clip_params& params
)
    : base_operation(), engine_(&engine), state_(&state), params_(params) {}

auto create_clip_operation::execute() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    (void)registry.create(params_.name);
    state_->anim.selected_clip_name          = params_.name;
    state_->ui.show_timeline                 = true;
    state_->anim.unsaved_clips[params_.name] = true;
}

auto create_clip_operation::undo() -> void {
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    registry.remove(params_.name);
    state_->anim.selected_clip_name.clear();
    state_->anim.unsaved_clips.erase(params_.name);
}

}  // namespace vw::sculptor
