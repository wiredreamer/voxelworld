module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

clip_service::clip_service(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto clip_service::save_clip(
    const std::string& clip_name
) const -> bool {
    namespace fs = std::filesystem;

    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(clip_name);
    if (!clip) {
        return false;
    }

    const fs::path filepath = app_state::clip_dir() / std::format("{}.voxa", clip->get_name());
    asset::voxa_serializer serializer(*clip);
    const auto result = serializer.serialize(filepath);
    if (result.has_value()) {
        state_->anim.unsaved_clips[clip_name] = false;
    }
    return result.has_value();
}

auto clip_service::save_clip_as(
    const std::string& clip_name, const std::string& new_name
) const -> bool {
    namespace fs = std::filesystem;

    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    auto clip      = registry.get(clip_name);
    if (!clip) {
        return false;
    }

    const auto old_name = clip->get_name();
    registry.remove(old_name);
    clip->set_name(new_name);
    registry.add(new_name, clip);

    const fs::path filepath = app_state::clip_dir() / std::format("{}.voxa", new_name);
    asset::voxa_serializer serializer(*clip);
    const auto result = serializer.serialize(filepath);

    state_->anim.unsaved_clips.erase(old_name);
    state_->anim.unsaved_clips[new_name] = !result.has_value();

    if (state_->anim.clip_to_layer.contains(old_name)) {
        state_->anim.clip_to_layer[new_name] = state_->anim.clip_to_layer[old_name];
        state_->anim.clip_to_layer.erase(old_name);
    }

    if (state_->anim.clip_settings_map.contains(old_name)) {
        state_->anim.clip_settings_map[new_name] = state_->anim.clip_settings_map[old_name];
        state_->anim.clip_settings_map.erase(old_name);
    }

    if (state_->anim.selected_clip_name == old_name) {
        state_->anim.selected_clip_name = new_name;
    }

    return result.has_value();
}

auto clip_service::save_all_clips() const -> void {
    namespace fs = std::filesystem;

    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const fs::path asset_dir_path = app_state::clip_dir();

    for (const auto& [name, clip] : registry.all()) {
        if (!clip) {
            continue;
        }

        fs::path filepath = asset_dir_path / std::format("{}.voxa", name);
        asset::voxa_serializer serializer(*clip);
        if (serializer.serialize(filepath).has_value()) {
            state_->anim.unsaved_clips[name] = false;
        }
    }
}

auto clip_service::load_clip(
    const std::string& filename, bool ignore_rig
) const -> clip_load_report {
    namespace fs = std::filesystem;

    const fs::path filepath = app_state::clip_dir() / fs::path{filename};
    asset::voxa_deserializer deserializer;
    const auto result = deserializer.deserialize(filepath);
    if (!result) {
        return {.status = clip_load_status::file_error};
    }

    const auto& clip = *result;
    auto& world      = engine_->get_world();

    clip_load_report report;
    const auto root_it = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root_it != state_->scene.name_to_entity.end()) {
        report.rig = world.system<ecs::animation_system>().check_clip(root_it->second, *clip);
    }

    if (!report.rig.ok() && !ignore_rig) {
        report.status = clip_load_status::rig_mismatch;
        return report;
    }

    auto& registry = world.resource<asset::animation_clip_registry>();
    registry.add(clip->get_name(), clip);

    state_->anim.selected_clip_name = clip->get_name();
    state_->ui.show_timeline        = true;
    state_->anim.selected_track_name.clear();
    state_->anim.selected_keyframe_id = asset::invalid_keyframe_id;

    report.status = clip_load_status::loaded;
    return report;
}

auto clip_service::close_clip(
    const std::string& clip_name
) const -> void {
    close_clip_params params = {.name = clip_name};
    auto op = std::make_unique<close_clip_operation>(*engine_, *state_, params);
    op_manager_->execute(std::move(op));
}

auto clip_service::enter_animation_mode() -> void {
    if (state_->ctx.in_clip()) {
        return;
    }

    save_transforms();

    state_->ctx.leave_to(0);
    state_->ctx.enter(edit_context::clip());
}

auto clip_service::exit_animation_mode() -> void {
    stop_all_layers();
    restore_transforms();
    state_->ctx.leave_to(0);
    state_->anim.timeline_cursor = 0.f;
}

auto clip_service::force_exit_animation_mode() -> void {
    if (state_->ctx.in_clip()) {
        exit_animation_mode();
    }
    state_->ui.show_timeline = false;
}

auto clip_service::save_transforms() -> void {
    if (state_->anim.has_saved_transforms) {
        return;
    }

    auto& world = engine_->get_world();
    state_->anim.saved_transforms.clear();

    for (const auto& [name, ent] : state_->scene.name_to_entity) {
        if (world.has<ecs::transform_component>(ent)) {
            auto& tc                             = world.get<ecs::transform_component>(ent);
            state_->anim.saved_transforms[name] = tc.get_transform();
        }
    }

    state_->anim.has_saved_transforms = true;
}

auto clip_service::restore_transforms() -> void {
    if (!state_->anim.has_saved_transforms) {
        return;
    }

    auto& world            = engine_->get_world();
    auto& transform_sys = world.system<ecs::transform_system>();

    for (const auto& [name, t] : state_->anim.saved_transforms) {
        if (state_->scene.name_to_entity.contains(name)) {
            const auto ent = state_->scene.name_to_entity[name];
            if (world.has<ecs::transform_component>(ent)) {
                transform_sys.modify(ent).set_transform(t);
            }
        }
    }

    state_->anim.saved_transforms.clear();
    state_->anim.has_saved_transforms = false;
}

auto clip_service::reset_all() -> void {
    stop_all_layers();

    if (state_->anim.has_saved_transforms) {
        auto& world            = engine_->get_world();
        auto& transform_sys = world.system<ecs::transform_system>();
        for (const auto& [name, t] : state_->anim.saved_transforms) {
            if (state_->scene.name_to_entity.contains(name)) {
                const auto ent = state_->scene.name_to_entity[name];
                if (world.has<ecs::transform_component>(ent)) {
                    transform_sys.modify(ent).set_transform(t);
                }
            }
        }
    }

    state_->anim.timeline_cursor = 0.f;
}

auto clip_service::stop_layer_for_clip(
    const std::string& clip_name
) -> void {
    if (state_->scene.root_name.empty() ||
        !state_->scene.name_to_entity.contains(state_->scene.root_name)) {
        return;
    }

    auto root   = state_->scene.name_to_entity[state_->scene.root_name];
    auto& world = engine_->get_world();
    if (!world.has<ecs::animation_player_component>(root)) {
        return;
    }

    const auto layer_idx = state_->anim.get_layer_for_clip(clip_name);
    const auto& player   = world.get<ecs::animation_player_component>(root);
    if (player.has_layer(layer_idx) && player.get_layer(layer_idx).clip &&
        player.get_layer(layer_idx).clip->get_name() == clip_name) {
        world.system<ecs::animation_system>().modify_player(root).layer(layer_idx).stop();
    }
}

auto clip_service::stop_all_layers() -> void {
    if (state_->scene.root_name.empty() ||
        !state_->scene.name_to_entity.contains(state_->scene.root_name)) {
        return;
    }

    const auto root = state_->scene.name_to_entity[state_->scene.root_name];
    auto& world     = engine_->get_world();
    if (!world.has<ecs::animation_player_component>(root)) {
        return;
    }

    const auto& player = world.get<ecs::animation_player_component>(root);
    auto& anim_sys     = world.system<ecs::animation_system>();
    for (std::size_t i = 0; i < player.layer_count(); ++i) {
        if (player.has_layer(i)) {
            anim_sys.modify_player(root).layer(i).clear();
        }
    }
}

}  // namespace vw::sculptor
