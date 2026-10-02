module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

auto scene_state::clear_entities(
    world_type& world
) -> void {
    for (auto ent : entities) {
        world.destroy(ent);
    }
    entities.clear();
    name_to_entity.clear();
    entity_to_name.clear();
}

auto socket_state::socket_preview::destroy_entities(
    world_type& world
) -> void {
    for (auto ent : entities) {
        world.destroy(ent);
    }
    entities.clear();
}

auto socket_state::erase_preview(
    const std::string& key, world_type& world
) -> void {
    const auto it = socket_previews.find(key);
    if (it == socket_previews.end()) {
        return;
    }
    it->second.destroy_entities(world);
    socket_previews.erase(it);
}

auto socket_state::erase_previews_for(
    const std::string& entity_name, world_type& world
) -> void {
    const auto prefix = entity_name + ":";
    std::erase_if(socket_previews, [&](auto& kv) {
        if (kv.first.starts_with(prefix)) {
            kv.second.destroy_entities(world);
            return true;
        }
        return false;
    });
}

auto socket_state::clear_all(
    world_type& world
) -> void {
    for (auto& preview : socket_previews | std::views::values) {
        preview.destroy_entities(world);
    }
    socket_previews.clear();
}

auto app_state::reset(
    world_type& world
) -> void {
    scene.clear_entities(world);
    sockets.clear_all(world);

    auto kept_clipboard = std::move(clipboard);
    auto kept_mcp       = std::move(mcp);
    const bool kept_startup_modal_open = ui.startup_modal_open;

    *this                 = app_state{};
    clipboard             = std::move(kept_clipboard);
    mcp                   = std::move(kept_mcp);
    ui.startup_modal_open = kept_startup_modal_open;
}

auto animation_state::has_unsaved_clip(
    const std::string& name
) const -> bool {
    const auto it = unsaved_clips.find(name);
    return it != unsaved_clips.end() && it->second;
}

auto animation_state::has_any_unsaved_clip() const -> bool {
    return std::ranges::any_of(unsaved_clips | std::views::values, [](bool v) { return v; });
}

auto animation_state::get_layer_for_clip(
    const std::string& name
) const -> std::size_t {
    const auto it = clip_to_layer.find(name);
    return it != clip_to_layer.end() ? it->second : 0;
}

auto animation_state::get_clip_settings(
    const std::string& name
) const -> clip_settings {
    const auto it = clip_settings_map.find(name);
    return it != clip_settings_map.end() ? it->second : clip_settings{};
}

auto animation_state::get_clip_settings_mut(
    const std::string& name
) -> clip_settings& {
    return clip_settings_map[name];
}

auto app_state::prefab_dir() -> std::filesystem::path {
    return std::filesystem::path{asset_root_name} / asset::dirs::prefabs;
}

auto app_state::model_dir() -> std::filesystem::path {
    return std::filesystem::path{asset_root_name} / asset::dirs::models;
}

auto app_state::clip_dir() -> std::filesystem::path {
    return std::filesystem::path{asset_root_name} / asset::dirs::animations;
}

auto app_state::fsm_dir() -> std::filesystem::path {
    return std::filesystem::path{asset_root_name} / asset::dirs::fsm;
}

}  // namespace vw::sculptor
