module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_clip_events_operation::set_clip_events_operation(
    engine_type& eng, app_state& st, set_clip_events_params params
)
    : engine_(&eng), state_(&st), params_(std::move(params)) {}

auto set_clip_events_operation::execute() -> void {
    const auto clip =
        engine_->get_world().resource<asset::animation_clip_registry>().get(params_.clip_name);
    if (!clip) {
        return;
    }

    previous_ = clip->get_events();
    apply_(params_.events);
}

auto set_clip_events_operation::undo() -> void {
    apply_(previous_);
}

auto set_clip_events_operation::apply_(std::vector<asset::animation_event> events) -> void {
    const auto clip =
        engine_->get_world().resource<asset::animation_clip_registry>().get(params_.clip_name);
    if (!clip) {
        return;
    }

    clip->set_events(std::move(events));
    state_->anim.unsaved_clips[params_.clip_name] = true;
}

}  // namespace vw::sculptor
