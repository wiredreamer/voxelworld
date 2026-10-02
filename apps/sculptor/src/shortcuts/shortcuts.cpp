module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.platform;

namespace vw::sculptor {
namespace {

constexpr auto held_mods(plat::keyboard::mods mods) -> uint32 {
    constexpr uint32 meaningful = mod_shift | mod_ctrl | mod_alt | mod_super;
    return static_cast<uint32>(mods) & meaningful;
}

}  // namespace

auto match(
    const plat::key_press_event& ev
) -> std::optional<command> {
    const auto held = held_mods(ev.mods);

    const auto it = std::ranges::find_if(shortcuts, [&ev, held](const shortcut& entry) {
        return entry.key == ev.key && entry.mods == held;
    });

    if (it == shortcuts.end()) {
        return std::nullopt;
    }
    return it->cmd;
}

namespace {

auto moves_the_camera(
    command cmd
) -> bool {
    switch (cmd) {
        case command::view_toggle_projection:
        case command::view_from_plus_z:
        case command::view_from_minus_z:
        case command::view_from_plus_x:
        case command::view_from_minus_x:
        case command::view_from_plus_y:
        case command::view_from_minus_y:
        case command::view_from_iso: return true;
        default: return false;
    }
}

}  // namespace

auto is_available(
    command cmd, const app_state& state
) -> bool {
    if (moves_the_camera(cmd)) {
        return true;
    }

    if (state.ctx.in_paste()) {
        return cmd == command::confirm || cmd == command::cancel;
    }

    if (const auto tool = tool_of(cmd); tool != tools::invalid) {
        return state.ctx.allows_tool(tool);
    }

    const bool has_selection = state.ctx.allows_volume_edit() && state.volume.selection.has_value();

    switch (cmd) {
        case command::select_all: return state.ctx.allows_volume_edit();

        case command::copy:
        case command::cut:
        case command::erase_selection: return has_selection;

        case command::cancel:
            if (state.ctx.in_clip()) {
                return state.anim.selected_keyframe_id != asset::invalid_keyframe_id ||
                    !state.scene.selected_name.empty();
            }
            return has_selection ||
                (state.ctx.in_prefab() && !state.scene.selected_name.empty());

        case command::paste:
            return state.ctx.allows_volume_edit() && !state.clipboard.clip.empty();

        case command::confirm: return false;

        case command::gizmo_move:
        case command::gizmo_rotate:
        case command::gizmo_scale: return state.ctx.allows_node_select();

        case command::toggle_sockets: return state.ctx.shows(panels::sockets);

        case command::enter_animation:
            return !state.anim.selected_clip_name.empty() && !state.ctx.in_clip();

        case command::play_pause: return !state.anim.selected_clip_name.empty();
        case command::step_back:
        case command::step_forward: return state.ctx.in_clip() && state.ui.show_timeline;

        case command::record_key:
        case command::record_key_all:
            return state.ctx.in_clip() && !state.anim.selected_clip_name.empty() &&
                !state.scene.selected_name.empty();

        case command::prev_key:
        case command::next_key:
            return state.ctx.in_clip() && !state.anim.selected_clip_name.empty();

        default: return true;
    }
}

auto keys_of(
    command cmd
) -> std::string_view {
    const auto it = std::ranges::find_if(shortcuts, [cmd](const shortcut& entry) {
        return entry.cmd == cmd;
    });
    return it == shortcuts.end() ? std::string_view{} : it->keys;
}

auto tool_of(
    command cmd
) -> tools {
    switch (cmd) {
        case command::tool_select: return tools::select_entity;
        case command::tool_add_voxel: return tools::add_voxel;
        case command::tool_remove_voxel: return tools::remove_voxel;
        case command::tool_paint: return tools::paint_voxel;
        case command::tool_color_picker: return tools::color_picker;
        case command::tool_move_pivot: return tools::move_pivot;
        case command::tool_select_box: return tools::select_box;
        case command::tool_pose: return tools::pose;
        default: return tools::invalid;
    }
}

auto command_for_tool(
    tools tool
) -> std::optional<command> {
    switch (tool) {
        case tools::select_entity: return command::tool_select;
        case tools::add_voxel: return command::tool_add_voxel;
        case tools::remove_voxel: return command::tool_remove_voxel;
        case tools::paint_voxel: return command::tool_paint;
        case tools::color_picker: return command::tool_color_picker;
        case tools::move_pivot: return command::tool_move_pivot;
        case tools::select_box: return command::tool_select_box;
        case tools::pose: return command::tool_pose;
        case tools::place_paste:
        case tools::invalid: break;
    }
    return std::nullopt;
}

}  // namespace vw::sculptor
