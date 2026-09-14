module vw.asset;

import std;
import vw.core;

namespace vw::asset {

animation_track::animation_track(std::string target_name, float32 fps)
    : target_name_(std::move(target_name)), compiled_fps_(fps) {}

auto animation_track::recompile_if_needed() const -> void {
    if (!is_dirty_) {
        return;
    }

    cached_duration_ = 0.0F;
    for (const auto& channel_var : channels_) {
        std::visit(
            [&](const auto& channel) {
                const auto duration_result = channel.get_duration();
                if (duration_result && *duration_result > cached_duration_) {
                    cached_duration_ = *duration_result;
                }
            },
            channel_var);
    }

    if (cached_duration_ <= 0.0F && channels_.empty()) {
        is_dirty_ = false;
        return;
    }

    frame_time_ = 1.0F / compiled_fps_;

    const uint32 frame_count =
        cached_duration_ > 0.0F
            ? static_cast<uint32>(std::ceil(cached_duration_ / frame_time_)) + 1
            : 1;

    compiled_transforms_.clear();
    compiled_transforms_.reserve(frame_count);

    for (uint32 i = 0; i < frame_count; ++i) {
        const float32 time = static_cast<float32>(i) * frame_time_;

        transform t;

        for (const auto& channel_var : channels_) {
            std::visit(
                [&](const auto& channel) {
                    const auto value_result = channel.evaluate(time);
                    if (!value_result) {
                        return;
                    }

                    const auto property = channel.get_property();
                    auto value          = value_result.value();

                    if constexpr (std::is_same_v<decltype(value), vec3f>) {
                        switch (property) {
                            case animation_property::position:
                                t.set_position(value);
                                break;
                            case animation_property::scale:
                                t.set_scale(value);
                                break;
                            default:
                                break;
                        }
                    } else if constexpr (std::is_same_v<decltype(value), quat>) {
                        t.set_rotation(value);
                    }
                },
                channel_var);
        }

        compiled_transforms_.push_back(t);
    }

    is_dirty_ = false;
}

auto animation_track::get_transform(float32 time) const -> std::expected<transform, error_type> {
    recompile_if_needed();

    if (compiled_transforms_.empty()) {
        return std::unexpected(error_type::empty);
    }

    if (compiled_transforms_.size() == 1) {
        return compiled_transforms_[0];
    }

    const float32 frame_f  = time / frame_time_;
    const auto frame_index = static_cast<uint32>(frame_f);

    if (frame_index >= compiled_transforms_.size() - 1) {
        return compiled_transforms_.back();
    }

    const float32 frac = frame_f - static_cast<float32>(frame_index);

    return math::lerp(
        compiled_transforms_[frame_index], compiled_transforms_[frame_index + 1], frac);
}

auto animation_track::get_duration() const -> float32 {
    recompile_if_needed();
    return cached_duration_;
}

auto animation_track::get_frame_time() const -> float32 {
    recompile_if_needed();
    return frame_time_;
}

auto animation_track::add_impl(animation_channel_variant channel) -> void {
    animation_property prop{};
    std::visit([&](const auto& ch) { prop = ch.get_property(); }, channel);

    for (auto& existing_channel : channels_) {
        bool should_replace = false;
        std::visit(
            [&](const auto& ch) { should_replace = ch.get_property() == prop; }, existing_channel);

        if (should_replace) {
            existing_channel = std::move(channel);
            is_dirty_        = true;
            return;
        }
    }

    channels_.push_back(std::move(channel));
    is_dirty_ = true;
}

auto animation_track::remove_channel(animation_property prop) -> void {
    const auto removed = std::ranges::remove_if(channels_, [prop](const auto& channel_var) {
        bool matches = false;
        std::visit([&](const auto& ch) { matches = ch.get_property() == prop; }, channel_var);
        return matches;
    });

    if (!removed.empty()) {
        channels_.erase(removed.begin(), removed.end());
        is_dirty_ = true;
    }
}

auto animation_track::get_channel(animation_property prop) const
    -> const animation_channel_variant* {
    for (const auto& channel_var : channels_) {
        bool found = false;
        std::visit([&](const auto& channel) { found = channel.get_property() == prop; },
                   channel_var);

        if (found) {
            return &channel_var;
        }
    }
    return nullptr;
}

auto animation_track::get_channel_mut(animation_property prop) -> animation_channel_variant* {
    for (auto& channel_var : channels_) {
        bool found = false;
        std::visit([&](const auto& channel) { found = channel.get_property() == prop; },
                   channel_var);

        if (found) {
            return &channel_var;
        }
    }
    return nullptr;
}

auto animation_track::has_channel(animation_property prop) const -> bool {
    return get_channel(prop) != nullptr;
}

animation_clip::animation_clip(std::string name) : name_(std::move(name)) {}

auto animation_clip::add_track(animation_track track) -> void {
    const std::string_view target_name = track.get_target_name();

    for (auto& existing_track : tracks_) {
        if (existing_track.get_target_name() == target_name) {
            existing_track = std::move(track);
            return;
        }
    }

    tracks_.push_back(std::move(track));
}

auto animation_clip::get_track(std::string_view target_name) const -> const animation_track* {
    for (const auto& track : tracks_) {
        if (track.get_target_name() == target_name) {
            return &track;
        }
    }
    return nullptr;
}

auto animation_clip::get_track_mut(std::string_view target_name) -> animation_track* {
    for (auto& track : tracks_) {
        if (track.get_target_name() == target_name) {
            return &track;
        }
    }
    return nullptr;
}

auto animation_clip::has_track(std::string_view target_name) const -> bool {
    return get_track(target_name) != nullptr;
}

auto animation_clip::remove_track(std::string_view target_name) -> void {
    const auto removed = std::ranges::remove_if(tracks_, [target_name](const auto& track) {
        return track.get_target_name() == target_name;
    });
    tracks_.erase(removed.begin(), removed.end());
}

auto animation_clip::get_duration() const -> float32 {
    float32 max_duration = 0.0F;

    for (const auto& track : tracks_) {
        max_duration = std::max(max_duration, track.get_duration());
    }

    return max_duration;
}

auto animation_clip::set_name(std::string name) -> void {
    name_ = std::move(name);
}

auto animation_clip::set_rig(
    std::string rig
) -> void {
    rig_ = std::move(rig);
}

auto animation_clip::get_target_names() const -> std::unordered_set<std::string> {
    std::unordered_set<std::string> names;
    for (const auto& track : tracks_) {
        names.insert(track.get_target_name());
    }
    return names;
}

auto animation_clip_registry::create(std::string_view name) -> std::shared_ptr<animation_clip> {
    std::string name_str(name);
    auto clip                   = std::make_shared<animation_clip>(name_str);
    clips_[std::move(name_str)] = clip;
    return clip;
}

auto animation_clip_registry::add(std::string_view name, std::shared_ptr<animation_clip> clip) -> void {
    clips_[std::string(name)] = std::move(clip);
}

auto animation_clip_registry::get(std::string_view name) const
    -> std::shared_ptr<animation_clip> {
    const auto it = clips_.find(name);
    return it != clips_.end() ? it->second : nullptr;
}

auto animation_clip_registry::has(std::string_view name) const -> bool {
    return clips_.contains(name);
}

auto animation_clip_registry::remove(std::string_view name) -> void {
    const auto it = clips_.find(name);
    if (it != clips_.end()) {
        clips_.erase(it);
    }
}

auto animation_clip_registry::clear() -> void {
    clips_.clear();
}

auto animation_fsm::add_state(state_node state) -> void {
    auto name = state.name;
    states_.emplace(std::move(name), std::move(state));
}

auto animation_fsm::set_entry_state(std::string_view name) -> void {
    entry_state_   = std::string(name);
    current_state_ = entry_state_;
}

auto animation_fsm::get_current_state_node() const -> const state_node* {
    const auto it = states_.find(current_state_);
    return it != states_.end() ? &it->second : nullptr;
}

auto animation_fsm::evaluate(const animation_layer& layer, trigger_set& triggers)
    -> std::optional<transition_result> {
    const auto it = states_.find(current_state_);
    if (it == states_.end()) {
        return std::nullopt;
    }

    const auto& current = it->second;
    for (const auto& rule : current.transitions) {
        if (rule.wait_until_end && layer.state != animation_state::stopped) {
            continue;
        }
        if (rule.wait_until_blend && layer.is_blending()) {
            continue;
        }

        bool triggered = !rule.trigger_name.empty() && triggers.contains(rule.trigger_name);
        if (!triggered && rule.condition) {
            triggered = rule.condition();
        }
        if (!triggered) {
            continue;
        }

        const auto target_it = states_.find(rule.target_state);
        if (target_it == states_.end()) {
            continue;
        }

        if (!rule.trigger_name.empty()) {
            triggers.erase(rule.trigger_name);
        }

        const auto& target = target_it->second;
        return transition_result{
            .target_state    = rule.target_state,
            .clip            = target.clip,
            .loop_mode       = target.loop_mode,
            .playback_speed  = target.playback_speed,
            .blend           = rule.blend,
            .layer_blend_in  = target.layer_blend_in,
            .layer_blend_out = target.layer_blend_out,
        };
    }

    return std::nullopt;
}

auto animation_fsm::apply_transition(const transition_result& result) -> void {
    current_state_ = result.target_state;
}

}  // namespace vw::asset
