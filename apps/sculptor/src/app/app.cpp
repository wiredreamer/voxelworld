module;

#include <sculptor_version.h>
#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
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

    , model_library_(
          eng.get_world().resource<asset::model_registry>(), eng.get_voxel_registry(),
          app_state::asset_root_name
      )
    , file_service_(eng, state_, model_library_)
    , clip_service_(eng, state_, op_manager_)
    , playback_service_(eng, state_)
    , keyframe_service_(eng, state_, op_manager_)
    , fsm_service_(eng, state_, model_library_)

    , menu_bar_(eng, state_, op_manager_, file_service_)
    , breadcrumb_bar_(eng, state_, clip_service_)
    , tool_panel_(state_)
    , gizmo_panel_(state_)
    , voxel_palette_panel_(eng, state_)
    , entity_properties_panel_(eng, state_, op_manager_, model_library_)
    , socket_panel_(eng, state_, op_manager_, model_library_)
    , keyframe_properties_panel_(eng, state_, op_manager_, keyframe_service_)
    , entity_tree_panel_(eng, state_, op_manager_, model_library_)
    , timeline_panel_(eng, state_, op_manager_, clip_service_, keyframe_service_)
    , fsm_panel_(state_, op_manager_, fsm_service_)
    , startup_modal_(eng, state_)
    , new_file_modal_(eng, state_, op_manager_)
    , open_file_modal_(eng, state_, model_library_, op_manager_)
    , save_as_modal_(eng, state_, file_service_)
    , add_machine_modal_(eng, state_, op_manager_, fsm_service_)
    , shortcuts_modal_(state_)
    , create_clip_modal_(eng, state_, op_manager_)
    , open_clip_modal_(eng, state_, clip_service_) {
    init_asset_dirs_();

    auto& window   = eng.get_window();
    auto& camera   = eng.get_camera();
    auto& renderer = eng.get_renderer();

    tools_[tools::select_entity] = std::make_unique<select_entity_tool>(eng, state_, op_manager_);
    tools_[tools::add_voxel]     = std::make_unique<add_voxel_tool>(eng, state_, op_manager_);
    tools_[tools::remove_voxel]  = std::make_unique<remove_voxel_tool>(eng, state_, op_manager_);
    tools_[tools::paint_voxel]   = std::make_unique<paint_tool>(eng, state_, op_manager_);
    tools_[tools::color_picker]  = std::make_unique<color_picker_tool>(eng, state_, op_manager_);
    tools_[tools::move_pivot]    = std::make_unique<move_pivot_tool>(eng, state_, op_manager_);
    tools_[tools::pose]          = std::make_unique<pose_tool>(eng, state_, op_manager_);

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
    file_service_.collect_dirty_models();
    prune_contexts_();
    refresh_volume_bounds_();
    sync_visibility_();

    if (!state_.ctx.allows_tool(state_.tool.selected_tool)) {
        state_.tool.selected_tool = state_.ctx.default_tool();
    }

    if (state_.tool.selected_tool != active_tool_) {
        active_tool_ = state_.tool.selected_tool;
        tools_[active_tool_]->on_activate();
    }

    camera_controller_.update(delta_time);

    auto& renderer = get_engine().get_renderer();
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{100, 0, 0}, colors::blue_4);
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{0, 100, 0}, colors::green_4);
    renderer.draw_line(vec3f{0, 0, 0}, vec3f{0, 0, 100}, colors::red_4);

    render_volume_overlay_();

    state_.ui.left_offset   = 0.f;
    state_.ui.bottom_offset = 0.f;
    state_.ui.right_offset  = 0.f;

    if (state_.ui.need_enter_machine.has_value()) {
        static_cast<void>(fsm_service_.enter(*state_.ui.need_enter_machine));
        state_.ui.need_enter_machine.reset();
    }

    update_animation_context_();

    menu_bar_.render(delta_time);
    breadcrumb_bar_.render(delta_time);

    render_panels_(delta_time);

    startup_modal_.render(delta_time);
    new_file_modal_.render(delta_time);
    open_file_modal_.render(delta_time);
    save_as_modal_.render(delta_time);
    add_machine_modal_.render();

    if (state_.ui.need_create_clip_modal) {
        state_.ui.need_create_clip_modal = false;
        create_clip_modal_.open();
    }
    create_clip_modal_.render(delta_time);

    if (state_.ui.need_load_clip_modal) {
        state_.ui.need_load_clip_modal = false;
        open_clip_modal_.open();
    }
    open_clip_modal_.render();

    if (state_.ui.need_shortcuts_modal) {
        state_.ui.need_shortcuts_modal = false;
        shortcuts_modal_.open();
    }
    shortcuts_modal_.render();

    handle_animation_actions_();

    tools_[active_tool_]->render(delta_time);

    if (state_.file.filename != prev_filename_ ||
        state_.file.has_unsaved_changes != prev_unsaved_state_ ||
        state_.anim.selected_clip_name != prev_clip_name_ ||
        state_.anim.has_any_unsaved_clip() != prev_clip_unsaved_state_ ||
        state_.ctx.in_clip() != prev_in_clip_) {
        prev_filename_           = state_.file.filename;
        prev_unsaved_state_      = state_.file.has_unsaved_changes;
        prev_clip_name_          = state_.anim.selected_clip_name;
        prev_clip_unsaved_state_ = state_.anim.has_any_unsaved_clip();
        prev_in_clip_            = state_.ctx.in_clip();
        update_title_();
    }

#if 0
    ImGui::Begin("Shadow Map Debug");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    for (uint32 i = 0; i < gfx::shadow_map::cascade_count; ++i) {
        void* shadow_map_texture_id = renderer.get_shadow_map_texture_id(i);
        ImGui::Image(
            reinterpret_cast<ImTextureID>(shadow_map_texture_id),
            ImVec2(256, 256),
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImVec4(1, 1, 1, 1),
            ImVec4(0, 0, 0, 0)
        );
        if (i % 2 == 0) {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();
    ImGui::End();
#endif
}

auto app::render_panels_(
    float delta_time
) -> void {
    if (state_.ctx.shows(panels::gizmo)) {
        gizmo_panel_.render(delta_time);
    }
    if (state_.ctx.shows(panels::tools)) {
        tool_panel_.render(delta_time);
    }

    if (state_.ctx.shows(panels::timeline) && state_.ui.show_timeline) {
        timeline_panel_.render(delta_time);
    }
    if (state_.ctx.shows(panels::palette)) {
        voxel_palette_panel_.render(delta_time);
    }

    if (state_.ctx.shows(panels::properties)) {
        entity_properties_panel_.render(delta_time);
    }
    if (state_.ctx.shows(panels::entity_tree)) {
        entity_tree_panel_.render(delta_time);
    }
    if (state_.ctx.shows(panels::sockets) && state_.ui.show_sockets) {
        socket_panel_.render(delta_time);
    }

    if (state_.ctx.shows(panels::fsm)) {
        fsm_panel_.render(delta_time);
    }
    if (state_.ctx.shows(panels::keyframe)) {
        keyframe_properties_panel_.render(delta_time);
    }
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

    if (const auto cmd = match(ev)) {
        run_command_(*cmd);
    }

    tools_[active_tool_]->on_key_press(ev);
}

auto app::run_command_(
    command cmd
) -> void {
    if (!is_available(cmd, state_)) {
        return;
    }

    if (const auto tool = tool_of(cmd); tool != tools::invalid) {
        state_.tool.selected_tool = tool;
        return;
    }

    switch (cmd) {
        case command::undo: op_manager_.undo(); break;
        case command::redo: op_manager_.redo(); break;

        case command::file_new: state_.ui.need_new_file_modal = true; break;
        case command::file_open: state_.ui.need_open_file_modal = true; break;

        case command::file_save:
            if (state_.ctx.in_clip() && !state_.anim.selected_clip_name.empty()) {
                state_.ui.need_save_clip = true;
            } else {
                static_cast<void>(file_service_.save());
            }
            break;

        case command::file_save_as: state_.ui.need_save_as_modal = true; break;

        case command::gizmo_move: state_.tool.gizmo = gizmo_mode::translate; break;
        case command::gizmo_rotate: state_.tool.gizmo = gizmo_mode::rotate; break;
        case command::gizmo_scale: state_.tool.gizmo = gizmo_mode::scale; break;

        case command::toggle_sockets: state_.ui.show_sockets ^= true; break;
        case command::enter_animation: state_.ui.need_enter_animation = true; break;
        case command::toggle_timeline: state_.ui.show_timeline ^= true; break;

        case command::play_pause: state_.anim.need_toggle_playback = true; break;
        case command::step_back: state_.anim.need_step_backward = true; break;
        case command::step_forward: state_.anim.need_step_forward = true; break;

        case command::record_key: keyframe_service_.record_pose(false); break;
        case command::record_key_all: keyframe_service_.record_pose(true); break;
        case command::prev_key: keyframe_service_.step_to_key(false); break;
        case command::next_key: keyframe_service_.step_to_key(true); break;

        case command::tool_select:
        case command::tool_add_voxel:
        case command::tool_remove_voxel:
        case command::tool_paint:
        case command::tool_color_picker:
        case command::tool_move_pivot: break;
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

auto app::update_animation_context_() -> void {
    if (state_.ui.need_save_clip) {
        state_.ui.need_save_clip = false;
        if (!state_.anim.selected_clip_name.empty()) {
            static_cast<void>(clip_service_.save_clip(state_.anim.selected_clip_name));
        }
    }

    if (state_.ui.need_enter_animation) {
        state_.ui.need_enter_animation = false;
        if (!state_.anim.selected_clip_name.empty()) {
            clip_service_.enter_animation_mode();
            state_.ui.show_timeline = true;
        }
    }

    if (state_.ctx.in_clip() && state_.anim.selected_clip_name.empty()) {
        clip_service_.force_exit_animation_mode();
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

    if (state_.anim.need_record_key) {
        state_.anim.need_record_key = false;
        keyframe_service_.record_pose(false);
    }

    if (state_.anim.need_record_key_all) {
        state_.anim.need_record_key_all = false;
        keyframe_service_.record_pose(true);
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

        if (state_.ctx.in_clip()) {
            title += " | ANIMATION";
        }
        if (state_.file.has_unsaved_changes) {
            title += " | UNSAVED CHANGES";
        }
    }
    get_engine().get_window().set_title(title);
}

auto app::prune_contexts_() -> void {
    auto& stack = state_.ctx.stack;

    const auto machine_count = fsm_service_.machines().size();

    const auto gone = [this, machine_count](const edit_context& ctx) {
        if (ctx.kind == edit_kind::fsm) {
            return ctx.layer >= machine_count;
        }
        return ctx.kind == edit_kind::model &&
               !state_.scene.name_to_entity.contains(ctx.node_name);
    };

    const auto it = std::ranges::find_if(stack, gone);
    if (it != stack.end()) {
        stack.erase(it, stack.end());
    }
}

auto app::sync_visibility_() -> void {
    auto& scene = state_.scene;

    std::erase_if(scene.hidden_nodes, [&scene](const std::string& name) {
        return !scene.name_to_entity.contains(name);
    });

    const auto root = scene.name_to_entity.find(scene.root_name);
    if (root == scene.name_to_entity.end()) {
        return;
    }

    auto& world     = get_engine().get_world();
    auto& model_sys = world.system<ecs::model_system>();

    const auto edited = state_.ctx.kind() == edit_kind::model ? state_.ctx.node_name()
                                                              : std::string_view{};

    std::vector<std::pair<ecs::entity, bool>> pending{{root->second, false}};

    while (!pending.empty()) {
        const auto [ent, inherited] = pending.back();
        pending.pop_back();

        bool hidden      = inherited;
        bool is_edited   = false;
        const auto named = scene.entity_to_name.find(ent);
        if (named != scene.entity_to_name.end()) {
            hidden    = hidden || scene.hidden_nodes.contains(named->second);
            is_edited = !edited.empty() && named->second == edited;
        }

        const bool shown = !hidden || is_edited;
        if (world.has<ecs::model_component>(ent) &&
            world.get<ecs::model_component>(ent).is_visible() != shown) {
            model_sys.modify(ent).set_visible(shown);
        }

        if (world.has<ecs::hierarchy_component>(ent)) {
            for (const auto child : world.get<ecs::hierarchy_component>(ent).get_children()) {
                pending.emplace_back(child, hidden);
            }
        }
    }
}

auto app::refresh_volume_bounds_() -> void {
    auto& world = get_engine().get_world();

    const asset::model* model = nullptr;
    const auto it             = state_.scene.name_to_entity.find(state_.edited_node());
    if (it != state_.scene.name_to_entity.end() && world.has<ecs::model_component>(it->second)) {
        model = world.get<ecs::model_component>(it->second).get_model().get();
    }

    if (model == nullptr) {
        state_.volume.source = asset::invalid_model_identity;
        state_.volume.occupied.reset();
        return;
    }

    const auto id = model->get_identity();
    if (id == state_.volume.source) {
        return;
    }

    state_.volume.source   = id;
    state_.volume.occupied = asset::occupied_bounds(*model);
}

auto app::render_volume_overlay_() -> void {
    if (state_.ctx.kind() != edit_kind::model) {
        return;
    }

    auto& world   = get_engine().get_world();
    const auto it = state_.scene.name_to_entity.find(state_.edited_node());
    if (it == state_.scene.name_to_entity.end()) {
        return;
    }

    const auto ent = it->second;
    if (!world.has<ecs::model_component>(ent) || !world.has<ecs::transform_component>(ent)) {
        return;
    }

    const auto& model_comp     = world.get<ecs::model_component>(ent);
    const auto& transform_comp = world.get<ecs::transform_component>(ent);
    auto& renderer             = get_engine().get_renderer();

    if (state_.volume.occupied) {
        const auto& bounds = *state_.volume.occupied;
        const auto size    = bounds.size();

        const auto corner = vec3f{
            static_cast<float32>(bounds.min.x),
            static_cast<float32>(bounds.min.y),
            static_cast<float32>(bounds.min.z),
        };

        renderer.draw_box(
            ecs::model_matrix(transform_comp, model_comp) * math::translation_matrix(corner),
            vec3f{
                static_cast<float32>(size.x),
                static_cast<float32>(size.y),
                static_cast<float32>(size.z),
            },
            colors::amber_4
        );
    }

    const auto node       = transform_comp.get_world_matrix();
    constexpr float32 arm = 1.5f;

    renderer.draw_line(node * vec3f{-arm, 0.f, 0.f}, node * vec3f{arm, 0.f, 0.f}, colors::purple_4);
    renderer.draw_line(node * vec3f{0.f, -arm, 0.f}, node * vec3f{0.f, arm, 0.f}, colors::purple_4);
    renderer.draw_line(node * vec3f{0.f, 0.f, -arm}, node * vec3f{0.f, 0.f, arm}, colors::purple_4);
}

auto app::init_asset_dirs_() -> void {
    for (const auto& dir :
         {app_state::prefab_dir(), app_state::model_dir(), app_state::clip_dir(),
          app_state::fsm_dir()}) {
        if (std::filesystem::exists(dir)) {
            continue;
        }

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            log::critical(
                "Failed to create asset directory '{}': {}",  //
                dir.string(),
                ec.message()
            );
        }
    }
}

}  // namespace vw::sculptor
