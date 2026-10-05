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

constexpr std::string_view clip_extension = ".voxa";

[[nodiscard]] auto refuse(std::string message) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

[[nodiscard]] auto joined(const std::vector<std::string>& names) -> std::string {
    std::string list;
    for (const std::string& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list.empty() ? std::string{"none"} : list;
}

[[nodiscard]] auto is_plain_name(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char symbol) {
        const bool letter = (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z');
        const bool digit  = symbol >= '0' && symbol <= '9';
        return letter || digit || symbol == '_' || symbol == '-';
    });
}

[[nodiscard]] auto stem_of(std::string_view name) -> std::string_view {
    if (name.ends_with(clip_extension)) {
        name.remove_suffix(clip_extension.size());
    }
    return name;
}

[[nodiscard]] auto clip_files() -> std::vector<std::string> {
    namespace fs = std::filesystem;

    std::vector<std::string> stems;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(app_state::clip_dir(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == clip_extension) {
            stems.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(stems);
    return stems;
}

}  // namespace

auto clip_service::root_() const -> std::expected<ecs::entity, std::string> {
    if (state_->file.filename.empty()) {
        return refuse("no prefab is open; clips are edited on an open prefab");
    }

    const auto root = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root == state_->scene.name_to_entity.end()) {
        return refuse("the prefab has no nodes yet; a clip animates the nodes of a prefab");
    }
    if (state_->paste.active() || state_->ctx.in_paste()) {
        return refuse("a paste is being placed; confirm or cancel it in the editor");
    }
    return root->second;
}

auto clip_service::open_clips() const -> std::vector<std::string> {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();

    std::vector<std::string> names;
    for (const auto& name : registry.all() | std::views::keys) {
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

auto clip_service::find(std::string_view name) const
    -> std::expected<std::shared_ptr<asset::animation_clip>, std::string> {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();

    auto clip = registry.get(stem_of(name));
    if (!clip) {
        return refuse(std::format(
            "the clip '{}' is not open; open clips: {}; open one with clip_open or make one with "
            "clip_create",
            stem_of(name), joined(open_clips())
        ));
    }
    return clip;
}

auto clip_service::select(std::string_view name) -> outcome {
    if (const auto root = root_(); !root) {
        return refuse(root.error());
    }

    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }

    auto& anim = state_->anim;
    if (anim.selected_clip_name != (*clip)->get_name()) {
        anim.selected_clip_name = (*clip)->get_name();
        anim.selected_track_name.clear();
        anim.selected_keyframe_id = asset::invalid_keyframe_id;
        anim.timeline_cursor      = 0.0F;
    }

    enter_animation_mode();
    state_->ui.show_timeline = true;
    return {};
}

auto clip_service::create(std::string_view name, bool overwrite) -> outcome {
    namespace fs = std::filesystem;

    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const std::string stem{stem_of(name)};
    if (!is_plain_name(stem)) {
        return refuse(std::format(
            "'{}' cannot name a clip: use letters, digits, '_' and '-'", stem
        ));
    }

    auto& world = engine_->get_world();
    if (world.resource<asset::animation_clip_registry>().has(stem)) {
        return refuse(std::format("the clip '{}' is already open", stem));
    }

    std::error_code ec;
    const fs::path filepath = app_state::clip_dir() / std::format("{}{}", stem, clip_extension);
    if (!overwrite && fs::exists(filepath, ec)) {
        return refuse(std::format(
            "a clip file '{}' already exists; open it with clip_open, or pass overwrite: true to "
            "start it anew",
            stem
        ));
    }

    op_manager_->execute(std::make_unique<create_clip_operation>(
        *engine_, *state_, create_clip_params{.name = stem}
    ));

    if (world.has<ecs::rig_component>(*root)) {
        if (const auto clip = world.resource<asset::animation_clip_registry>().get(stem)) {
            clip->set_rig(world.get<ecs::rig_component>(*root).get_name());
        }
    }

    return select(stem);
}

auto clip_service::open(std::string_view name, bool ignore_rig) -> outcome {
    if (const auto root = root_(); !root) {
        return refuse(root.error());
    }

    const std::string stem{stem_of(name)};
    if (engine_->get_world().resource<asset::animation_clip_registry>().has(stem)) {
        return select(stem);
    }

    const auto report = load_clip(std::format("{}{}", stem, clip_extension), ignore_rig);
    switch (report.status) {
        case clip_load_status::loaded:
            break;
        case clip_load_status::file_error:
            return refuse(std::format(
                "the clip '{}' could not be read; the clip files are: {}", stem, joined(clip_files())
            ));
        case clip_load_status::rig_mismatch:
            if (!report.rig.rig_matches()) {
                return refuse(std::format(
                    "the clip '{}' is made for the rig '{}' and the prefab has the rig '{}'; pass "
                    "ignore_rig: true to open it anyway",
                    stem, report.rig.clip_rig, report.rig.rig
                ));
            }
            return refuse(std::format(
                "the clip '{}' animates targets the prefab does not have: {}; pass ignore_rig: "
                "true to open it anyway",
                stem, joined(report.rig.unknown_targets)
            ));
    }

    return select(stem);
}

auto clip_service::save(std::string_view name) -> outcome {
    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }
    if (!save_clip((*clip)->get_name())) {
        return refuse(std::format("the clip '{}' could not be written", (*clip)->get_name()));
    }
    return {};
}

auto clip_service::close(std::string_view name, bool discard_unsaved) -> outcome {
    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }

    const std::string clip_name = (*clip)->get_name();
    if (!discard_unsaved && state_->anim.has_unsaved_clip(clip_name)) {
        return refuse(std::format(
            "the clip '{}' has unsaved changes; save it with clip_save or pass discard_unsaved: "
            "true",
            clip_name
        ));
    }

    stop_layer_for_clip(clip_name);
    close_clip(clip_name);

    if (state_->anim.selected_clip_name.empty()) {
        force_exit_animation_mode();
    }
    return {};
}

auto clip_service::set_tracks(std::string_view name, std::vector<asset::animation_track> tracks)
    -> outcome {
    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }

    auto targets = engine_->get_world().system<ecs::animation_system>().collect_targets(*root);
    std::ranges::sort(targets);

    for (const asset::animation_track& track : tracks) {
        if (!std::ranges::binary_search(targets, track.get_target_name())) {
            return refuse(std::format(
                "'{}' is not an animation target of this prefab; the targets are: {}; make a node "
                "a target with node_set_components and anim_target",
                track.get_target_name(), joined(targets)
            ));
        }
    }

    if (auto selected = select((*clip)->get_name()); !selected) {
        return selected;
    }

    op_manager_->execute(std::make_unique<set_clip_tracks_operation>(
        *engine_, *state_,
        set_clip_tracks_params{.clip_name = (*clip)->get_name(), .tracks = std::move(tracks)}
    ));
    return {};
}

auto clip_service::set_events(std::string_view name, std::vector<asset::animation_event> events)
    -> outcome {
    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }

    asset::animation_clip checked = **clip;
    checked.set_events(events);
    if (const auto problems = asset::find_problems(checked); !problems.empty()) {
        return refuse(std::format(
            "{}; an event name is one word such as hit.start, its payload one word or empty, "
            "and its time within the clip, 0 to {:.3g} s",
            joined(problems), (*clip)->get_duration()
        ));
    }

    if (auto selected = select((*clip)->get_name()); !selected) {
        return selected;
    }

    op_manager_->execute(std::make_unique<set_clip_events_operation>(
        *engine_, *state_,
        set_clip_events_params{.clip_name = (*clip)->get_name(), .events = std::move(events)}
    ));
    return {};
}

auto clip_service::retarget(std::string_view name, std::string_view from, std::string_view to)
    -> outcome {
    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }

    std::vector<asset::animation_track> tracks = (*clip)->get_tracks();

    const auto moved = std::ranges::find(tracks, from, &asset::animation_track::get_target_name);
    if (moved == tracks.end()) {
        std::vector<std::string> keyed;
        for (const asset::animation_track& track : tracks) {
            keyed.push_back(track.get_target_name());
        }
        return refuse(std::format(
            "the clip '{}' has no track for '{}'; its tracks are: {}", (*clip)->get_name(), from,
            joined(keyed)
        ));
    }
    if (from == to) {
        return {};
    }
    if (std::ranges::contains(tracks, to, &asset::animation_track::get_target_name)) {
        return refuse(std::format(
            "the clip '{}' already has a track for '{}'; remove its keys with clip_remove_keys "
            "first",
            (*clip)->get_name(), to
        ));
    }

    const auto targets = engine_->get_world().system<ecs::animation_system>().collect_targets(*root);
    if (!std::ranges::contains(targets, to)) {
        return refuse(std::format(
            "'{}' is not an animation target of this prefab; the targets are: {}", to,
            joined(targets)
        ));
    }

    *moved = moved->retargeted(std::string{to});

    if (auto selected = select((*clip)->get_name()); !selected) {
        return selected;
    }

    op_manager_->execute(std::make_unique<set_clip_tracks_operation>(
        *engine_, *state_,
        set_clip_tracks_params{.clip_name = (*clip)->get_name(), .tracks = std::move(tracks)}
    ));
    return {};
}

auto clip_service::layer_for_(std::string_view name) -> std::expected<clip_layer, std::string> {
    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }
    if (auto selected = select((*clip)->get_name()); !selected) {
        return refuse(selected.error());
    }

    auto& world          = engine_->get_world();
    auto& anim_sys       = world.system<ecs::animation_system>();
    const auto layer_idx = state_->anim.get_layer_for_clip((*clip)->get_name());

    if (!world.has<ecs::animation_player_component>(*root)) {
        world.modify(*root).with<ecs::animation_player_component>();
    }

    const auto& player  = world.get<ecs::animation_player_component>(*root);
    const bool on_layer = player.has_layer(layer_idx) && player.get_layer(layer_idx).clip == *clip;
    if (!on_layer) {
        anim_sys.modify_player(*root).layer(layer_idx).blend_to(*clip, std::nullopt);
    }

    const auto& settings = state_->anim.get_clip_settings((*clip)->get_name());
    const auto layer     = anim_sys.modify_player(*root).layer(layer_idx);
    layer.set_playback_speed(settings.playback_speed);
    layer.set_loop_mode(settings.loop_mode);

    return clip_layer{.root = *root, .clip = *clip, .index = layer_idx};
}

auto clip_service::show_pose(std::string_view name, float32 time) -> outcome {
    if (time < 0.0F) {
        return refuse(std::format("the time must not be negative, got {}", time));
    }

    const auto on = layer_for_(name);
    if (!on) {
        return refuse(on.error());
    }

    auto posed = engine_->get_world().system<ecs::animation_system>().modify_player(on->root);
    posed.layer(on->index).pause();
    posed.layer(on->index).set_time(time);
    posed.apply_pose();

    state_->anim.timeline_cursor = time;
    state_->anim.need_apply_pose = false;
    return {};
}

auto clip_service::play(std::string_view name, const clip_playback& how) -> outcome {
    if (how.from < 0.0F) {
        return refuse(std::format("the start time must not be negative, got {}", how.from));
    }
    if (how.speed && !(*how.speed > 0.0F && std::isfinite(*how.speed))) {
        return refuse(std::format("the speed must be a positive number, got {}", *how.speed));
    }

    const auto clip = find(name);
    if (!clip) {
        return refuse(clip.error());
    }
    if (how.from > (*clip)->get_duration()) {
        return refuse(std::format(
            "the start time {} is past the end of the clip, {} s", how.from, (*clip)->get_duration()
        ));
    }

    auto& settings = state_->anim.get_clip_settings_mut((*clip)->get_name());
    if (how.loop) {
        settings.loop_mode = *how.loop;
    }
    if (how.speed) {
        settings.playback_speed = *how.speed;
    }

    const auto on = layer_for_(name);
    if (!on) {
        return refuse(on.error());
    }

    const auto layer =
        engine_->get_world().system<ecs::animation_system>().modify_player(on->root).layer(on->index);
    layer.stop();
    layer.play();
    layer.set_time(how.from);

    state_->anim.timeline_cursor = how.from;
    state_->anim.need_apply_pose = false;
    return {};
}

auto clip_service::stop(std::string_view name) -> outcome {
    return show_pose(name, 0.0F);
}

auto clip_service::playback(std::string_view name) const -> clip_playback_status {
    const auto& settings = state_->anim.get_clip_settings(std::string{name});

    clip_playback_status status{
        .state = asset::animation_state::stopped,
        .time  = 0.0F,
        .loop  = settings.loop_mode,
        .speed = settings.playback_speed,
    };

    const auto root = root_();
    const auto clip = find(name);
    if (!root || !clip) {
        return status;
    }

    const auto& world = engine_->get_world();
    if (!world.has<ecs::animation_player_component>(*root)) {
        return status;
    }

    const auto& player   = world.get<ecs::animation_player_component>(*root);
    const auto layer_idx = state_->anim.get_layer_for_clip((*clip)->get_name());
    if (!player.has_layer(layer_idx) || player.get_layer(layer_idx).clip != *clip) {
        return status;
    }

    const auto& layer = player.get_layer(layer_idx);
    status.state      = layer.state;
    status.time       = layer.time;
    return status;
}

}  // namespace vw::sculptor
