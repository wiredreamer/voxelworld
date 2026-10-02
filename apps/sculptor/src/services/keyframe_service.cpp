module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {
namespace {

constexpr float32 same_instant_tolerance_seconds = asset::same_instant_seconds;

constexpr std::array pose_properties{
    asset::animation_property::position,
    asset::animation_property::rotation,
    asset::animation_property::scale,
};

auto property_of(gizmo_mode mode) -> asset::animation_property {
    switch (mode) {
        case gizmo_mode::rotate: return asset::animation_property::rotation;
        case gizmo_mode::scale: return asset::animation_property::scale;
        case gizmo_mode::translate: break;
    }
    return asset::animation_property::position;
}

auto time_of(const keyframe_value& value) -> float32 {
    return std::visit([](const auto& kf) { return kf.time; }, value);
}

auto id_of(const keyframe_value& value) -> uint32 {
    return std::visit([](const auto& kf) { return kf.id(); }, value);
}

}  // namespace

keyframe_service::keyframe_service(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto keyframe_service::pose_value_(
    ecs::entity ent, asset::animation_property property, float32 time
) const -> keyframe_value {
    const auto& tc = engine_->get_world().get<ecs::transform_component>(ent);

    switch (property) {
        case asset::animation_property::rotation:
            return asset::keyframe_quat(time, tc.get_rotation());
        case asset::animation_property::scale:
            return asset::keyframe_vec3f(time, tc.get_scale());
        default: break;
    }
    return asset::keyframe_vec3f(time, tc.get_position());
}

auto keyframe_service::key_at_(
    const std::string& track_name, asset::animation_property property, float32 time
) const -> std::optional<keyframe_value> {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return std::nullopt;
    }

    const auto* track = clip->get_track(track_name);
    if (track == nullptr) {
        return std::nullopt;
    }

    const auto* channel_var = track->get_channel(property);
    if (channel_var == nullptr) {
        return std::nullopt;
    }

    return std::visit(
        [time](const auto& channel) -> std::optional<keyframe_value> {
            for (const auto& kf : channel.get_keyframes()) {
                if (std::abs(kf.time - time) < same_instant_tolerance_seconds) {
                    return keyframe_value(kf);
                }
            }
            return std::nullopt;
        },
        *channel_var
    );
}

auto keyframe_service::place_keyframe_(
    const std::string& track_name, asset::animation_property property,
    const keyframe_value& keyframe, const std::optional<keyframe_value>& replaced,
    bool track_exists
) const -> std::vector<std::unique_ptr<base_operation>> {
    const auto& clip_name = state_->anim.selected_clip_name;

    std::vector<std::unique_ptr<base_operation>> parts;

    if (!track_exists) {
        parts.push_back(
            std::make_unique<add_track_operation>(
                *engine_,
                *state_,
                add_track_params{
                    .clip_name  = clip_name,
                    .track_name = track_name,
                    .property   = property,
                    .keyframe   = keyframe,
                }
            )
        );
        return parts;
    }

    const auto occupant = key_at_(track_name, property, time_of(keyframe));
    const bool occupied_by_other =
        occupant.has_value() && (!replaced || id_of(*occupant) != id_of(*replaced));

    std::optional<keyframe_value> rewritten = replaced;
    if (!rewritten && occupied_by_other) {
        rewritten = occupant;
    }

    if (occupied_by_other && replaced) {
        parts.push_back(
            std::make_unique<remove_keyframe_operation>(
                *engine_,
                *state_,
                remove_keyframe_params{
                    .clip_name  = clip_name,
                    .track_name = track_name,
                    .property   = property,
                    .keyframe   = *occupant,
                }
            )
        );
    }

    if (rewritten) {
        parts.push_back(
            std::make_unique<modify_keyframe_operation>(
                *engine_,
                *state_,
                modify_keyframe_params{
                    .clip_name    = clip_name,
                    .track_name   = track_name,
                    .property     = property,
                    .old_keyframe = *rewritten,
                    .new_keyframe = keyframe,
                }
            )
        );
        return parts;
    }

    parts.push_back(
        std::make_unique<add_keyframe_operation>(
            *engine_,
            *state_,
            add_keyframe_params{
                .clip_name  = clip_name,
                .track_name = track_name,
                .property   = property,
                .keyframe   = keyframe,
            }
        )
    );
    return parts;
}

auto keyframe_service::execute_parts_(
    std::vector<std::unique_ptr<base_operation>> parts
) -> void {
    if (parts.empty()) {
        return;
    }

    if (parts.size() == 1) {
        op_manager_->execute(std::move(parts.front()));
        return;
    }

    op_manager_->execute(std::make_unique<composite_operation>(std::move(parts)));
}

auto keyframe_service::set_keyframe(
    const std::string& track_name, asset::animation_property property,
    const keyframe_value& keyframe
) -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    execute_parts_(
        place_keyframe_(track_name, property, keyframe, std::nullopt, clip->has_track(track_name))
    );
}

auto keyframe_service::modify_keyframe(
    const std::string& track_name, asset::animation_property property,
    const keyframe_value& old_keyframe, const keyframe_value& new_keyframe
) -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip || !clip->has_track(track_name)) {
        return;
    }

    execute_parts_(place_keyframe_(track_name, property, new_keyframe, old_keyframe, true));
}

auto keyframe_service::preview_keyframe(
    const std::string& track_name, asset::animation_property property,
    const keyframe_value& keyframe
) -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    auto* track = clip->get_track_mut(track_name);
    if (!track) {
        return;
    }

    auto* channel_var = track->get_channel_mut(property);
    if (!channel_var) {
        return;
    }

    const uint32 id = std::visit([](const auto& kf) -> uint32 { return kf.id(); }, keyframe);

    if (property == asset::animation_property::rotation) {
        auto& channel = std::get<asset::animation_channel<quat>>(*channel_var);
        channel.replace(id, std::get<asset::keyframe_quat>(keyframe));
    } else {
        auto& channel = std::get<asset::animation_channel<vec3f>>(*channel_var);
        channel.replace(id, std::get<asset::keyframe_vec3f>(keyframe));
    }

    track->mark_dirty();
    state_->anim.need_apply_pose = true;
}

auto keyframe_service::has_key_at_cursor() const -> bool {
    if (state_->scene.selected_name.empty()) {
        return false;
    }
    return key_at_(
               state_->scene.selected_name,
               property_of(state_->tool.gizmo),
               state_->anim.timeline_cursor
    )
        .has_value();
}

auto keyframe_service::record_pose(
    bool all_channels
) -> void {
    if (state_->anim.selected_clip_name.empty() || state_->scene.selected_name.empty()) {
        return;
    }

    auto& world          = engine_->get_world();
    const auto& registry = world.resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    const auto name = state_->scene.selected_name;
    const auto it   = state_->scene.name_to_entity.find(name);
    if (it == state_->scene.name_to_entity.end() ||
        !world.has<ecs::transform_component>(it->second)) {
        return;
    }

    const float32 time = state_->anim.timeline_cursor;

    std::vector<asset::animation_property> properties;
    if (all_channels) {
        properties.assign(pose_properties.begin(), pose_properties.end());
    } else {
        properties.push_back(property_of(state_->tool.gizmo));
    }

    bool track_exists = clip->has_track(name);

    std::vector<std::unique_ptr<base_operation>> parts;

    for (const auto property : properties) {
        auto placed = place_keyframe_(
            name, property, pose_value_(it->second, property, time), std::nullopt, track_exists
        );
        track_exists = true;
        std::ranges::move(placed, std::back_inserter(parts));
    }

    if (parts.empty()) {
        return;
    }

    state_->anim.selected_track_name = name;

    execute_parts_(std::move(parts));
}

auto keyframe_service::step_to_key(
    bool forward
) -> void {
    if (state_->anim.selected_clip_name.empty()) {
        return;
    }

    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    const auto& track_name = state_->anim.selected_track_name.empty() ?
        state_->scene.selected_name :
        state_->anim.selected_track_name;

    const auto* track = clip->get_track(track_name);
    if (track == nullptr) {
        return;
    }

    const float32 from = state_->anim.timeline_cursor;
    std::optional<float32> best;

    for (const auto property : pose_properties) {
        const auto* channel_var = track->get_channel(property);
        if (channel_var == nullptr) {
            continue;
        }

        std::visit(
            [&best, from, forward](const auto& channel) {
                for (const auto& kf : channel.get_keyframes()) {
                    const bool ahead = forward ? kf.time > from + same_instant_tolerance_seconds
                                               : kf.time < from - same_instant_tolerance_seconds;
                    if (!ahead) {
                        continue;
                    }
                    if (!best || (forward ? kf.time < *best : kf.time > *best)) {
                        best = kf.time;
                    }
                }
            },
            *channel_var
        );
    }

    if (best) {
        state_->anim.timeline_cursor = *best;
    }
}

auto keyframe_service::move_keyframe(
    const std::string& track_name, asset::animation_property property, uint32 keyframe_id,
    float32 time
) -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    const auto* track = clip->get_track(track_name);
    if (track == nullptr) {
        return;
    }

    const auto* channel_var = track->get_channel(property);
    if (channel_var == nullptr) {
        return;
    }

    const auto found = std::visit(
        [keyframe_id](const auto& channel) -> std::optional<keyframe_value> {
            for (const auto& kf : channel.get_keyframes()) {
                if (kf.id() == keyframe_id) {
                    return keyframe_value(kf);
                }
            }
            return std::nullopt;
        },
        *channel_var
    );

    if (!found || std::abs(time_of(*found) - time) < same_instant_tolerance_seconds) {
        return;
    }

    auto moved = *found;
    std::visit([time](auto& kf) { kf.time = time; }, moved);

    execute_parts_(place_keyframe_(track_name, property, moved, *found, true));
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
