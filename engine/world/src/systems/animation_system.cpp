module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

namespace detail {
constexpr log::log_category animation_system_lc{"animation_system"};
}  // namespace detail

animation_system::animation_system(world& w)
    : world_(&w) {}

auto animation_system::get_target_fps() const -> float32 {
    return 1.0f / target_frame_time_;
}

auto animation_system::set_target_fps(
    float32 fps
) -> void {
    target_frame_time_ = 1.0f / fps;
}

auto animation_system::update(
    float32 delta_time
) -> void {
    clear_fired_events_();

    accumulated_delta_time_ += delta_time;

    if (accumulated_delta_time_ < target_frame_time_) {
        return;
    }

    float32 effective_delta = target_frame_time_;
    accumulated_delta_time_ -= target_frame_time_;
    if (accumulated_delta_time_ > target_frame_time_) {
        accumulated_delta_time_ = target_frame_time_;
    }

    to_remove_.clear();

    auto& reg = world_->registry();
    for (entity ent : active_entities_) {
        if (!reg.has<animation_player_component>(ent)) {
            to_remove_.push_back(ent);
            continue;
        }

        auto& anim_comp = reg.get<animation_player_component>(ent);

        process_animation(ent, anim_comp, effective_delta);

        if (!anim_comp.is_any_playing()) {
            to_remove_.push_back(ent);
        }
    }

    for (entity ent : to_remove_) {
        remove_active_entity(ent);
    }
}

auto animation_system::add_active_entity(
    entity root_ent
) -> void {
    auto [it, inserted] = active_entities_.insert(root_ent);

    if (inserted) {
        build_and_cache_target_map(root_ent);
    }

    warn_unknown_targets_(root_ent);
}

auto animation_system::remove_active_entity(
    entity root_ent
) -> void {
    active_entities_.erase(root_ent);
    target_maps_.erase(root_ent);
    warned_targets_.erase(root_ent);
}

auto animation_system::build_and_cache_target_map(
    entity root_ent
) -> void {
    target_maps_[root_ent] = collect_target_map_(root_ent);

    warned_targets_.erase(root_ent);
}

auto animation_system::collect_target_list_(
    entity root_ent
) const -> std::vector<std::pair<std::string, entity>> {
    std::vector<std::pair<std::string, entity>> targets;

    std::deque<entity> to_visit;
    to_visit.push_back(root_ent);

    auto& reg = world_->registry();
    while (!to_visit.empty()) {
        entity current = to_visit.front();
        to_visit.pop_front();

        if (reg.has<animation_target_component>(current)) {
            const auto& target_comp = reg.get<animation_target_component>(current);
            targets.emplace_back(target_comp.get_name(), current);
        }

        if (reg.has<hierarchy_component>(current)) {
            const auto& hierarchy = reg.get<hierarchy_component>(current);
            for (entity child : hierarchy.get_children()) {
                to_visit.push_back(child);
            }
        }
    }

    return targets;
}

auto animation_system::collect_target_map_(
    entity root_ent
) const -> std::unordered_map<std::string, entity> {
    std::unordered_map<std::string, entity> target_map;
    for (auto& [name, ent] : collect_target_list_(root_ent)) {
        target_map[name] = ent;
    }
    return target_map;
}

auto animation_system::collect_targets(
    entity root_ent
) const -> std::vector<std::string> {
    std::vector<std::string> names;
    for (auto& [name, ent] : collect_target_list_(root_ent)) {
        names.push_back(std::move(name));
    }
    return names;
}

auto animation_system::check_clip(
    entity root_ent, const asset::animation_clip& clip
) const -> rig_report {
    rig_report report;
    report.clip_rig = clip.get_rig();

    auto& reg = world_->registry();
    if (reg.has<rig_component>(root_ent)) {
        report.rig = reg.get<rig_component>(root_ent).get_name();
    }

    const auto targets = collect_target_map_(root_ent);
    for (const auto& track : clip.get_tracks()) {
        if (!targets.contains(track.get_target_name())) {
            report.unknown_targets.push_back(track.get_target_name());
        }
    }

    return report;
}

auto animation_system::warn_unknown_targets_(
    entity root_ent
) -> void {
    const auto* target_map = get_cached_target_map(root_ent);
    if (target_map == nullptr) {
        return;
    }

    auto& reg = world_->registry();
    if (!reg.has<animation_player_component>(root_ent)) {
        return;
    }

    const auto& player = reg.get<animation_player_component>(root_ent);
    auto& warned       = warned_targets_[root_ent];

    for (std::size_t i = 0; i < player.layer_count(); ++i) {
        const auto& clip = player.get_layer(i).clip;
        if (!clip) {
            continue;
        }

        for (const auto& track : clip->get_tracks()) {
            const auto& name = track.get_target_name();
            if (target_map->contains(name) || !warned.insert(name).second) {
                continue;
            }

            log::warn(
                detail::animation_system_lc,
                "clip '{}' animates target '{}', which this rig has no node for",
                clip->get_name(), name
            );
        }
    }
}

auto animation_system::get_cached_target_map(
    entity root_ent
) const -> const std::unordered_map<std::string, entity>* {
    auto it = target_maps_.find(root_ent);
    if (it != target_maps_.end()) {
        return &it->second;
    }
    return nullptr;
}

auto animation_system::update_layer_time(
    asset::animation_layer& layer, float32 delta_time
) -> void {
    asset::advance_layer_time(layer, delta_time, crossed_events_);
}

auto animation_system::clear_fired_events_() -> void {
    auto& reg = world_->registry();
    for (const entity ent : entities_with_fired_events_) {
        if (reg.has<animation_player_component>(ent)) {
            reg.get<animation_player_component>(ent).fired_events_.clear();
        }
    }
    entities_with_fired_events_.clear();
}

auto animation_system::collect_fired_events_(
    entity ent, animation_player_component& anim_comp, std::size_t layer_index
) -> void {
    if (crossed_events_.empty()) {
        return;
    }

    if (anim_comp.fired_events_.empty()) {
        entities_with_fired_events_.push_back(ent);
    }

    const auto& clip_name = anim_comp.layers_[layer_index].clip->get_name();
    for (const auto* event : crossed_events_) {
        anim_comp.fired_events_.push_back({
            .layer   = layer_index,
            .clip    = clip_name,
            .name    = event->name,
            .payload = event->payload,
        });
    }
    crossed_events_.clear();
}

auto animation_system::process_layer(
    asset::animation_layer& layer, float32 delta_time, bool is_base
) -> void {
    if (!layer.clip) {
        return;
    }

    if (layer.blend_transition.duration > 0.0f &&
        layer.blend_elapsed < layer.blend_transition.duration) {
        layer.blend_elapsed += delta_time;

        if (layer.blend_prev_clip) {
            layer.blend_prev_time +=
                delta_time * layer.blend_prev_playback_speed * layer.blend_prev_direction;
            float32 prev_duration = layer.blend_prev_clip->get_duration();
            if (layer.blend_prev_time > prev_duration) {
                layer.blend_prev_time = prev_duration;
            } else if (layer.blend_prev_time < 0.0f) {
                layer.blend_prev_time = 0.0f;
            }
        }

        if (layer.blend_elapsed >= layer.blend_transition.duration) {
            layer.blend_prev_clip  = nullptr;
            layer.blend_elapsed    = 0.0f;
            layer.blend_transition = {};
            layer.blend_snapshot.clear();
        }
    }

    if (layer.fade_is_out) {
        layer.fade_elapsed += delta_time;
        if (layer.fade_out.duration > 0.0f && layer.fade_elapsed < layer.fade_out.duration) {
            float32 t = layer.fade_elapsed / layer.fade_out.duration;
            t         = math::apply_easing_bezier(
                t, layer.fade_out.interp, layer.fade_out.tangent_in, layer.fade_out.tangent_out
            );
            layer.fade_influence = 1.0f - t;
        } else {
            layer.fade_influence = 0.0f;
            layer.fade_is_out    = false;
            layer.state          = asset::animation_state::stopped;
        }
        if (layer.state == asset::animation_state::playing) {
            update_layer_time(layer, delta_time);
        }
        return;
    }

    if (layer.state != asset::animation_state::playing) {
        return;
    }

    if (!is_base && layer.fade_influence < 1.0f) {
        if (layer.fade_in.duration > 0.0f) {
            layer.fade_elapsed += delta_time;
            if (layer.fade_elapsed < layer.fade_in.duration) {
                float32 t = layer.fade_elapsed / layer.fade_in.duration;
                t         = math::apply_easing_bezier(
                    t, layer.fade_in.interp, layer.fade_in.tangent_in, layer.fade_in.tangent_out
                );
                layer.fade_influence = t;
            } else {
                layer.fade_influence = 1.0f;
            }
        } else {
            layer.fade_influence = 1.0f;
        }
    }

    update_layer_time(layer, delta_time);

    if (!is_base && layer.state == asset::animation_state::stopped) {
        if (layer.fade_out.duration > 0.0f) {
            layer.fade_is_out  = true;
            layer.fade_elapsed = 0.0f;
        } else {
            layer.fade_influence = 0.0f;
        }
    }
}

auto animation_system::compute_layer_transform(
    const asset::animation_layer& layer, const std::string& target_name, const transform& rest
) const -> std::optional<transform> {
    if (!layer.clip) {
        return std::nullopt;
    }

    const auto* track = layer.clip->get_track(target_name);
    if (!track) {
        return std::nullopt;
    }

    return compute_track_transform(layer, *track, target_name, rest);
}

auto animation_system::compute_track_transform(
    const asset::animation_layer& layer, const asset::animation_track& track,
    const std::string& target_name, const transform& rest
) const -> std::optional<transform> {
    auto transform_result = track.get_transform(layer.time);
    if (!transform_result) {
        return std::nullopt;
    }

    transform t = merge_with_rest(*transform_result, track, rest);

    bool is_blending = layer.blend_transition.duration > 0.0f &&
        (layer.blend_prev_clip || !layer.blend_snapshot.empty());
    if (is_blending) {
        float32 blend_factor =
            math::clamp(layer.blend_elapsed / layer.blend_transition.duration, 0.0f, 1.0f);
        blend_factor = math::apply_easing_bezier(
            blend_factor,
            layer.blend_transition.interp,
            layer.blend_transition.tangent_in,
            layer.blend_transition.tangent_out
        );

        auto snapshot_it = layer.blend_snapshot.find(target_name);
        if (snapshot_it != layer.blend_snapshot.end()) {
            t = math::lerp(snapshot_it->second, t, blend_factor);
        } else if (layer.blend_prev_clip) {
            auto* prev_track = layer.blend_prev_clip->get_track(target_name);
            if (prev_track) {
                auto prev_result = prev_track->get_transform(layer.blend_prev_time);
                if (prev_result) {
                    transform prev_merged = merge_with_rest(*prev_result, *prev_track, rest);
                    t = math::lerp(prev_merged, t, blend_factor);
                }
            }
        }
    }

    return t;
}

auto animation_system::process_animation(
    entity ent, animation_player_component& anim_comp, float32 delta_time
) -> void {
    for (std::size_t i = 0; i < anim_comp.layers_.size(); ++i) {
        crossed_events_.clear();
        process_layer(anim_comp.layers_[i], delta_time, i == 0);
        collect_fired_events_(ent, anim_comp, i);
    }

    apply_animation(ent, anim_comp);
}

auto animation_system::apply_animation(
    entity root_ent, const animation_player_component& anim_comp
) -> void {
    const auto* target_map = get_cached_target_map(root_ent);
    if (!target_map) {
        return;
    }

    auto& reg = world_->registry();

    const auto target_of = [&](const std::string& name) -> entity {
        const auto it = target_map->find(name);
        return it != target_map->end() ? it->second : invalid_entity;
    };

    const auto rest_of = [&](entity target) -> transform {
        if (!target.is_valid()) {
            return {};
        }

        const auto* comp = reg.try_get<animation_target_component>(target);
        return comp != nullptr ? comp->get_rest_transform() : transform{};
    };

    auto& final_transforms = final_transforms_;
    final_transforms.clear();

    constexpr uint32 no_slot = std::numeric_limits<uint32>::max();

    const auto slot_of = [&](entity target) -> transform* {
        if (target.index >= slot_by_entity_.size()) {
            return nullptr;
        }

        const auto slot = slot_by_entity_[target.index];
        if (slot >= final_transforms.size()) {
            return nullptr;
        }

        auto& [ent, value] = final_transforms[slot];
        return ent == target ? &value : nullptr;
    };

    const auto add_slot = [&](entity target, const transform& value) {
        if (target.index >= slot_by_entity_.size()) {
            slot_by_entity_.resize(static_cast<std::size_t>(target.index) + 1, no_slot);
        }

        slot_by_entity_[target.index] = static_cast<uint32>(final_transforms.size());
        final_transforms.emplace_back(target, value);
    };

    if (!anim_comp.layers_.empty()) {
        const auto& base = anim_comp.layers_[0];
        if (base.clip) {
            for (const auto& track : base.clip->get_tracks()) {
                const auto& name  = track.get_target_name();
                const auto target = target_of(name);
                if (!target.is_valid()) {
                    continue;
                }
                auto rest = rest_of(target);
                auto t    = compute_track_transform(base, track, name, rest);
                if (t) {
                    add_slot(target, *t);
                }
            }

            if (base.blend_transition.duration > 0.0f && base.blend_prev_clip) {
                float32 blend_factor =
                    math::clamp(base.blend_elapsed / base.blend_transition.duration, 0.0f, 1.0f);
                blend_factor = math::apply_easing_bezier(
                    blend_factor,
                    base.blend_transition.interp,
                    base.blend_transition.tangent_in,
                    base.blend_transition.tangent_out
                );

                for (const auto& prev_track : base.blend_prev_clip->get_tracks()) {
                    const auto& name  = prev_track.get_target_name();
                    const auto target = target_of(name);
                    if (!target.is_valid() || slot_of(target) != nullptr) {
                        continue;
                    }

                    auto rest = rest_of(target);

                    auto snapshot_it = base.blend_snapshot.find(name);
                    if (snapshot_it != base.blend_snapshot.end()) {
                        add_slot(target, math::lerp(snapshot_it->second, rest, blend_factor));
                    } else {
                        auto prev_result = prev_track.get_transform(base.blend_prev_time);
                        if (prev_result) {
                            transform prev_merged =
                                merge_with_rest(*prev_result, prev_track, rest);
                            add_slot(target, math::lerp(prev_merged, rest, blend_factor));
                        }
                    }
                }
            }
        }
    }

    for (std::size_t i = 1; i < anim_comp.layers_.size(); ++i) {
        const auto& layer = anim_comp.layers_[i];
        if (!layer.clip || layer.fade_influence <= 0.0f) {
            continue;
        }

        for (const auto& target_name : layer.mask) {
            const auto target = target_of(target_name);
            if (!target.is_valid()) {
                continue;
            }

            auto rest = rest_of(target);
            auto t    = compute_layer_transform(layer, target_name, rest);
            if (!t) {
                continue;
            }

            if (auto* existing = slot_of(target)) {
                *existing = math::lerp(*existing, *t, layer.fade_influence);
            } else {
                add_slot(target, math::lerp(rest, *t, layer.fade_influence));
            }
        }
    }

    auto& transform_sys = world_->system<transform_system>();
    for (const auto& [target_ent, t] : final_transforms) {
        if (reg.try_get<transform_component>(target_ent) == nullptr) {
            continue;
        }

        auto modifier = transform_sys.modify(target_ent);
        modifier.set_transform_with_matrix(t, t.calc_matrix());
    }
}

animation_system::player_modifier::player_modifier(
    animation_system* system, entity ent, animation_player_component* component
)
    : system_(system), entity_(ent), component_(component) {}

auto animation_system::modify_player(
    entity ent
) -> player_modifier {
    auto& comp = world_->registry().get<animation_player_component>(ent);
    return player_modifier(this, ent, &comp);
}

auto animation_system::player_modifier::add_layer(
    std::size_t index
) const -> void {
    if (index >= component_->layers_.size()) {
        component_->layers_.resize(index + 1);
    }
}

auto animation_system::player_modifier::layer(
    std::size_t index
) -> layer_modifier {
    add_layer(index);
    return layer_modifier(system_, entity_, &component_->layers_[index]);
}

auto animation_system::player_modifier::apply_pose() const -> void {
    if (!system_->get_cached_target_map(entity_)) {
        system_->build_and_cache_target_map(entity_);
    }
    system_->apply_animation(entity_, *component_);
}

auto animation_system::player_modifier::rebuild_target_map() const -> void {
    system_->build_and_cache_target_map(entity_);
}

animation_system::layer_modifier::layer_modifier(
    animation_system* system, entity ent, asset::animation_layer* layer
)
    : system_(system), entity_(ent), layer_(layer) {}

auto animation_system::layer_modifier::play() const -> void {
    if (layer_->state != asset::animation_state::playing) {
        layer_->state          = asset::animation_state::playing;
        layer_->time           = 0.0f;
        layer_->direction      = 1.0f;
        layer_->fade_influence = 1.0f;
        layer_->fade_is_out    = false;
        layer_->fade_elapsed   = 0.0f;

        system_->add_active_entity(entity_);
    }
}

auto animation_system::layer_modifier::play(
    const asset::transition& fade_in
) const -> void {
    layer_->fade_in        = fade_in;
    layer_->fade_influence = 0.0f;
    layer_->fade_is_out    = false;
    layer_->fade_elapsed   = 0.0f;

    if (layer_->state != asset::animation_state::playing) {
        layer_->state     = asset::animation_state::playing;
        layer_->time      = 0.0f;
        layer_->direction = 1.0f;

        system_->add_active_entity(entity_);
    }
}

auto animation_system::layer_modifier::pause() const -> void {
    if (layer_->state == asset::animation_state::playing) {
        layer_->state = asset::animation_state::paused;
    }
}

auto animation_system::layer_modifier::stop() const -> void {
    layer_->state          = asset::animation_state::stopped;
    layer_->time           = 0.0f;
    layer_->fade_influence = 0.0f;
    layer_->fade_is_out    = false;
}

auto animation_system::layer_modifier::clear() const -> void {
    *layer_ = asset::animation_layer{};
}

auto animation_system::layer_modifier::stop(
    const asset::transition& fade_out
) const -> void {
    layer_->fade_out     = fade_out;
    layer_->fade_is_out  = true;
    layer_->fade_elapsed = 0.0f;
}

auto animation_system::layer_modifier::resume() const -> void {
    if (layer_->state == asset::animation_state::paused) {
        layer_->state = asset::animation_state::playing;
        system_->add_active_entity(entity_);
    }
}

auto animation_system::layer_modifier::set_time(
    float32 time
) const -> void {
    layer_->time = time;
}

auto animation_system::layer_modifier::set_playback_speed(
    float32 speed
) const -> void {
    layer_->playback_speed = speed;
}

auto animation_system::layer_modifier::set_loop_mode(
    asset::animation_loop_mode mode
) const -> void {
    layer_->loop_mode = mode;
}

auto animation_system::layer_modifier::set_fade_in(
    const asset::transition& t
) const -> void {
    layer_->fade_in = t;
}

auto animation_system::layer_modifier::set_fade_out(
    const asset::transition& t
) const -> void {
    layer_->fade_out = t;
}

auto animation_system::layer_modifier::blend_to(
    std::shared_ptr<asset::animation_clip> clip, std::optional<asset::transition> t
) const -> void {
    asset::transition trans = t.value_or(layer_->blend_transition);

    if (trans.duration > 0.0f && layer_->clip) {
        if (layer_->blend_transition.duration > 0.0f && layer_->clip) {
            float32 bf =
                math::clamp(layer_->blend_elapsed / layer_->blend_transition.duration, 0.0f, 1.0f);
            bf = math::apply_easing_bezier(
                bf,
                layer_->blend_transition.interp,
                layer_->blend_transition.tangent_in,
                layer_->blend_transition.tangent_out
            );

            const auto* target_map = system_->get_cached_target_map(entity_);

            auto& reg = system_->world_->registry();
            auto get_rest = [&](const std::string& name) -> transform {
                if (!target_map) {
                    return {};
                }
                auto it = target_map->find(name);
                if (it != target_map->end() &&
                    reg.has<animation_target_component>(it->second)) {
                    return reg.get<animation_target_component>(it->second)
                        .get_rest_transform();
                }
                return {};
            };

            auto old_snapshot = std::move(layer_->blend_snapshot);
            layer_->blend_snapshot.clear();

            for (const auto& track : layer_->clip->get_tracks()) {
                const auto& name = track.get_target_name();
                auto cur_result  = track.get_transform(layer_->time);
                if (!cur_result) {
                    continue;
                }

                auto rest             = get_rest(name);
                transform blended = merge_with_rest(*cur_result, track, rest);

                auto snapshot_it = old_snapshot.find(name);
                if (snapshot_it != old_snapshot.end()) {
                    blended = math::lerp(snapshot_it->second, blended, bf);
                } else if (layer_->blend_prev_clip) {
                    auto* prev_track = layer_->blend_prev_clip->get_track(name);
                    if (prev_track) {
                        auto prev_result = prev_track->get_transform(layer_->blend_prev_time);
                        if (prev_result) {
                            transform prev_merged =
                                merge_with_rest(*prev_result, *prev_track, rest);
                            blended = math::lerp(prev_merged, blended, bf);
                        }
                    }
                }

                layer_->blend_snapshot[name] = blended;
            }
        } else {
            layer_->blend_snapshot.clear();
        }

        layer_->blend_prev_clip           = layer_->clip;
        layer_->blend_prev_time           = layer_->time;
        layer_->blend_prev_playback_speed = layer_->playback_speed;
        layer_->blend_prev_direction      = layer_->direction;
    } else {
        layer_->blend_prev_clip = nullptr;
        layer_->blend_snapshot.clear();
    }

    layer_->clip             = std::move(clip);
    layer_->time             = 0.0f;
    layer_->blend_elapsed    = 0.0f;
    layer_->blend_transition = trans;

    if (layer_->clip) {
        layer_->mask = layer_->clip->get_target_names();
    } else {
        layer_->mask.clear();
    }

    if (layer_->state != asset::animation_state::playing) {
        layer_->state          = asset::animation_state::playing;
        layer_->direction      = 1.0f;
        layer_->fade_is_out    = false;
        layer_->fade_influence = 1.0f;
        layer_->fade_elapsed   = 0.0f;
    }
    system_->add_active_entity(entity_);
}

auto animation_system::layer_modifier::blend_to_by_name(
    std::string_view name, std::optional<asset::transition> t
) -> void {
    auto clip = system_->world_->resource<asset::animation_clip_registry>().get(name);
    if (clip) {
        blend_to(std::move(clip), t);
    }
}

animation_system::target_modifier::target_modifier(
    entity ent, animation_target_component* component
)
    : entity_(ent), component_(component) {}

auto animation_system::modify_target(
    entity ent
) -> target_modifier {
    auto& comp = world_->registry().get<animation_target_component>(ent);
    return target_modifier(ent, &comp);
}

auto animation_system::target_modifier::set_target_name(
    std::string name
) const -> void {
    component_->target_name_ = std::move(name);
}

auto animation_system::target_modifier::set_rest_transform(
    const transform& rest
) const -> void {
    component_->rest_transform_ = rest;
}

animation_system::rig_modifier::rig_modifier(
    entity ent, rig_component* component
)
    : entity_(ent), component_(component) {}

auto animation_system::modify_rig(
    entity ent
) -> rig_modifier {
    auto& comp = world_->registry().get<rig_component>(ent);
    return rig_modifier(ent, &comp);
}

auto animation_system::rig_modifier::set_name(
    std::string name
) const -> void {
    component_->rig_name_ = std::move(name);
}

auto animation_system::merge_with_rest(
    const transform& anim, const asset::animation_track& track, const transform& rest
) -> transform {
    auto has_keyframes = [&](asset::animation_property prop) -> bool {
        const auto* ch = track.get_channel(prop);
        if (!ch) {
            return false;
        }
        return std::visit([](const auto& c) { return c.keyframe_count() > 0; }, *ch);
    };

    transform result = anim;

    if (!has_keyframes(asset::animation_property::position)) {
        result.set_position(rest.get_position());
    }
    if (!has_keyframes(asset::animation_property::rotation)) {
        result.set_rotation(rest.get_rotation());
    }
    if (!has_keyframes(asset::animation_property::scale)) {
        result.set_scale(rest.get_scale());
    }

    return result;
}

}  // namespace vw::ecs