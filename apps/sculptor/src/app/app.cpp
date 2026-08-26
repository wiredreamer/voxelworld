module;

#include <sculptor_version.h>
#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

app::app(
    engine_type& eng
)
    : gfx::app(eng)
    , camera_controller_(0.1f, 5.0f)

    , file_service_(eng, state_)
    , clip_service_(eng, state_, op_manager_)
    , playback_service_(eng, state_)
    , keyframe_service_(eng, state_, op_manager_)

    , menu_bar_(eng, state_, op_manager_, file_service_)
    , tool_panel_(state_)
    , block_palette_panel_(eng, state_)
    , entity_properties_panel_(eng, state_, op_manager_)
    , socket_panel_(eng, state_, op_manager_)
    , keyframe_properties_panel_(eng, state_, op_manager_)
    , entity_tree_panel_(eng, state_, op_manager_)
    , clip_manager_panel_(eng, state_, op_manager_, clip_service_)
    , timeline_panel_(eng, state_, op_manager_, clip_service_, keyframe_service_)
    , startup_modal_(eng, state_)
    , new_file_modal_(eng, state_)
    , open_file_modal_(eng, state_)
    , save_as_modal_(eng, state_, file_service_) {
    init_asset_dir_();

    auto& window   = eng.get_window();
    auto& camera   = eng.get_camera();
    auto& renderer = eng.get_renderer();

    tools_[tools::select_entity] = std::make_unique<select_entity_tool>(eng, state_);
    tools_[tools::add_voxel]     = std::make_unique<add_voxel_tool>(eng, state_, op_manager_);
    tools_[tools::remove_voxel]  = std::make_unique<remove_voxel_tool>(eng, state_, op_manager_);
    tools_[tools::paint_voxel]   = std::make_unique<paint_tool>(eng, state_, op_manager_);
    tools_[tools::color_picker]  = std::make_unique<color_picker_tool>(eng, state_, op_manager_);

    camera_controller_.setup(window, camera);
    camera_controller_.set_camera_speed(20.f);

    window.sub<plat::key_press_event>([this](const plat::key_press_event& ev) -> bool {
        handle_key_press(ev);
        return true;
    });

    window.sub<plat::window_close_event>([this](const plat::window_close_event&) -> bool {
        get_engine().shutdown();
        return true;
    });

    window.sub<plat::mouse_move_event>([this](const plat::mouse_move_event& ev) -> bool {
        handle_mouse_move(ev);
        return true;
    });
    window.sub<plat::mouse_press_event>([this](const plat::mouse_press_event& ev) -> bool {
        handle_mouse_press(ev);
        return true;
    });
    window.sub<plat::mouse_release_event>([this](const plat::mouse_release_event& ev) -> bool {
        handle_mouse_release(ev);
        return true;
    });

    camera.set_position({25.0f, 25.0f, 25.0f});
    camera.set_rotation(-30.0f, -135.0f);

    renderer.set_clear_color(vec4f{0.15f, 0.27f, 0.45f, 1.0f});

    auto& dir_light_settings     = renderer.get_directional_light_settings();
    dir_light_settings.direction = math::normalize(vec3f{+0.4f, -1.0f, +0.4f});
}

app::~app() {
    state_.scene.clear_entities(get_engine().get_world());
    state_.sockets.clear_all(get_engine().get_world());
}

auto app::render(
    float delta_time
) -> void {
    if (state_.tool.selected_tool != active_tool_) {
        active_tool_ = state_.tool.selected_tool;
        tools_[active_tool_]->on_activate();
    }

    camera_controller_.update(delta_time);

    auto& renderer = get_engine().get_renderer();
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{100, 0, 0}, colors::blue);
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{0, 100, 0}, colors::green);
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{0, 0, 100}, colors::red);

    state_.ui.left_top_voffset    = 0.f;
    state_.ui.left_bottom_voffset = 0.f;
    state_.ui.right_top_voffset   = 0.f;

    menu_bar_.render(delta_time);

    // left side
    tool_panel_.render(delta_time);

    if (state_.ui.show_timeline) {
        timeline_panel_.render(delta_time);
    }
    block_palette_panel_.render(delta_time);

    // right side
    entity_properties_panel_.render(delta_time);
    entity_tree_panel_.render(delta_time);
    if (state_.ui.show_sockets) {
        socket_panel_.render(delta_time);
    }
    if (state_.ui.need_create_clip_modal || state_.ui.need_load_clip_modal) {
        state_.ui.show_clip_manager = true;
    }
    if (state_.ui.show_clip_manager) {
        clip_manager_panel_.render(delta_time);
    } else if (state_.anim.animation_mode) {
        clip_service_.force_exit_animation_mode();
    }
    keyframe_properties_panel_.render(delta_time);

    // modals
    startup_modal_.render(delta_time);
    new_file_modal_.render(delta_time);
    open_file_modal_.render(delta_time);
    save_as_modal_.render(delta_time);

    handle_animation_actions_();

    tools_[active_tool_]->render(delta_time);

    if (state_.file.filename != prev_filename_ ||
        state_.file.has_unsaved_changes != prev_unsaved_state_ ||
        state_.anim.selected_clip_name != prev_clip_name_ ||
        state_.anim.has_any_unsaved_clip() != prev_clip_unsaved_state_ ||
        state_.anim.animation_mode != prev_animation_mode_) {
        prev_filename_           = state_.file.filename;
        prev_unsaved_state_      = state_.file.has_unsaved_changes;
        prev_clip_name_          = state_.anim.selected_clip_name;
        prev_clip_unsaved_state_ = state_.anim.has_any_unsaved_clip();
        prev_animation_mode_     = state_.anim.animation_mode;
        update_title_();
    }

#if 0
    ImGui::Begin("Shadow Map Debug");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    // Сетка 2x2 для всех каскадов
    for (uint32 i = 0; i < gfx::shadow_map::cascade_count; ++i) {
        void* shadow_map_texture_id = renderer.get_shadow_map_texture_id(i);
        ImGui::Image(
            reinterpret_cast<ImTextureID>(shadow_map_texture_id),
            ImVec2(256, 256),  // Уменьшенный размер для сетки
            ImVec2(0, 0),      // UV координаты верхнего левого угла
            ImVec2(1, 1),      // UV координаты нижнего правого угла
            ImVec4(1, 1, 1, 1), // tint цвет
            ImVec4(0, 0, 0, 0)  // border цвет
        );
        if (i % 2 == 0) {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();
    ImGui::End();
#endif
}

auto app::handle_key_press(
    const plat::key_press_event& ev
) -> void {
    const auto& io = ImGui::GetIO();

    const bool really_want_keyboard =  //
        ImGui::IsAnyItemActive() || ImGui::IsAnyItemFocused();
    if ((io.WantCaptureKeyboard && really_want_keyboard) || camera_movement_enabled_) {
        return;
    }

    using keys = plat::keyboard::keys;
    using mods = plat::keyboard::mods;

    if (ev.key == keys::Z && ev.with(mods::CTRL) && !ev.with(mods::SHIFT)) {
        op_manager_.undo();
    }
    if (ev.key == keys::Z && ev.with(mods::CTRL) && ev.with(mods::SHIFT)) {
        op_manager_.redo();
    }

    if (ev.key == keys::KEY_0) {
        state_.tool.selected_tool = tools::select_entity;
    }
    if (ev.key == keys::KEY_1) {
        state_.tool.selected_tool = tools::add_voxel;
    }
    if (ev.key == keys::KEY_2) {
        state_.tool.selected_tool = tools::remove_voxel;
    }
    if (ev.key == keys::KEY_3) {
        state_.tool.selected_tool = tools::paint_voxel;
    }
    if (ev.key == keys::KEY_4) {
        state_.tool.selected_tool = tools::color_picker;
    }

    tools_[active_tool_]->on_key_press(ev);

    if (ev.key == keys::S && ev.with(mods::ALT)) {
        state_.ui.show_sockets ^= true;
    }
    if (ev.key == keys::A && ev.with(mods::ALT)) {
        state_.ui.show_clip_manager ^= true;
    }
    if (ev.key == keys::T && ev.with(mods::ALT)) {
        state_.ui.show_timeline ^= true;
    }
    if (ev.key == keys::SPACE && !ev.with(mods::CTRL)) {
        state_.anim.need_toggle_playback = true;
    }
    if (ev.key == keys::LEFT && !ev.with(mods::CTRL) && state_.ui.show_timeline) {
        state_.anim.need_step_backward = true;
    }
    if (ev.key == keys::RIGHT && !ev.with(mods::CTRL) && state_.ui.show_timeline) {
        state_.anim.need_step_forward = true;
    }

    handle_file_shortcuts(ev);
}

auto app::handle_file_shortcuts(
    const plat::key_press_event& ev
) -> void {
    using keys = plat::keyboard::keys;
    using mods = plat::keyboard::mods;

    if (ev.key == keys::N && ev.with(mods::CTRL)) {
        state_.ui.need_new_file_modal = true;
    }
    if (ev.key == keys::O && ev.with(mods::CTRL)) {
        state_.ui.need_open_file_modal = true;
    }
    if (ev.key == keys::S && ev.with(mods::CTRL) && !ev.with(mods::SHIFT)) {
        if (state_.anim.animation_mode && !state_.anim.selected_clip_name.empty()) {
            state_.ui.need_save_clip = true;
        } else {
            file_service_.save();
        }
    }
    if (ev.key == keys::S && ev.with(mods::CTRL) && ev.with(mods::SHIFT)) {
        state_.ui.need_save_as_modal = true;
    }
}

auto app::handle_mouse_move(
    const plat::mouse_move_event& ev
) -> void {
    const auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    if (!camera_movement_enabled_) {
        tools_[active_tool_]->on_mouse_move(ev);
    }
}

auto app::handle_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    if (!camera_movement_enabled_ && ev.button == plat::mouse::buttons::RIGHT) {
        camera_movement_enabled_ = true;
        camera_controller_.set_mouse_captured(camera_movement_enabled_);
        camera_controller_.set_keyboard_control_enabled(camera_movement_enabled_);

        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;

        ImGui::CloseCurrentPopup();
        ImGui::SetWindowFocus(nullptr);

        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    }

    if (!camera_movement_enabled_) {
        tools_[active_tool_]->on_mouse_press(ev);
    }
}

auto app::handle_mouse_release(
    const plat::mouse_release_event& ev
) -> void {
    auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    if (camera_movement_enabled_ && ev.button == plat::mouse::buttons::RIGHT) {
        camera_movement_enabled_ = false;
        camera_controller_.set_mouse_captured(camera_movement_enabled_);
        camera_controller_.set_keyboard_control_enabled(camera_movement_enabled_);

        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    }

    if (!camera_movement_enabled_) {
        tools_[active_tool_]->on_mouse_release(ev);
    }
}

auto app::handle_animation_actions_() -> void {
    if (state_.anim.need_toggle_playback) {
        state_.anim.need_toggle_playback = false;
        playback_service_.toggle_playback();
    }

    if (state_.anim.need_stop_playback) {
        state_.anim.need_stop_playback = false;
        playback_service_.stop_playback();
    }

    if (state_.anim.need_add_keyframe) {
        state_.anim.need_add_keyframe = false;
        keyframe_service_.add_keyframe();
    }

    if (state_.anim.need_delete_keyframe) {
        state_.anim.need_delete_keyframe = false;
        keyframe_service_.delete_keyframe();
    }
}

auto app::update_title_() -> void {
    std::string title;
    if (state_.file.filename.empty()) {
        title = std::format("Sculptor {}", version_string);
    } else {
        auto filename_suffix = state_.file.has_unsaved_changes ? "*" : "";
        title                = std::format(
            "Sculptor {} | {}{}", version_string, state_.file.filename, filename_suffix
        );

        if (!state_.anim.selected_clip_name.empty()) {
            auto clip_suffix =
                state_.anim.has_unsaved_clip(state_.anim.selected_clip_name) ? "*" : "";
            title += std::format(" | {}.voxa{}", state_.anim.selected_clip_name, clip_suffix);
        }

        if (state_.anim.animation_mode) {
            title += " | ANIMATION";
        }
        if (state_.file.has_unsaved_changes) {
            title += " | UNSAVED CHANGES";
        }
    }
    get_engine().get_window().set_title(title);
}

auto app::init_asset_dir_() -> void {
    if (!std::filesystem::exists(app_state::asset_dir_name)) {
        std::error_code ec;
        std::filesystem::create_directories(app_state::asset_dir_name, ec);
        if (ec) {
            log::critical(
                "Failed to create asset directory '{}': {}",  //
                app_state::asset_dir_name,
                ec.message()
            );
        }
    }
}

}  // namespace vw::sculptor
