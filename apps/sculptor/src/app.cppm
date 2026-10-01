export module vw.sculptor:app;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;
import :shortcuts;
import :operations;
import :services;
import :tools;
import :ui;

export namespace vw::sculptor {

class app final : public gfx::app {
public:
    using engine_type = gfx::engine;

    explicit app(engine_type& eng);
    ~app() override;

    auto render(float delta_time) -> void override;

private:
    auto handle_key_press(const plat::key_press_event& ev) -> void;
    auto run_command_(command cmd) -> void;
    auto cancel_() -> void;
    auto handle_mouse_move(const plat::mouse_move_event& ev) -> void;
    auto handle_mouse_press(const plat::mouse_press_event& ev) -> void;
    auto handle_mouse_release(const plat::mouse_release_event& ev) -> void;

    auto render_panels_(float delta_time) -> void;
    auto update_animation_context_() -> void;
    auto sync_visibility_() -> void;
    auto handle_animation_actions_() -> void;
    auto prune_contexts_() -> void;
    auto refresh_volume_bounds_() -> void;
    auto render_volume_overlay_() -> void;
    auto update_title_() -> void;
    static auto init_asset_dirs_() -> void;

    gfx::free_camera_controller camera_controller_;
    bool camera_movement_enabled_ = false;
    bool prev_unsaved_state_      = false;
    bool prev_clip_unsaved_state_ = false;
    bool prev_in_clip_            = false;
    std::string prev_clip_name_;
    std::string prev_filename_;

    app_state state_;
    operation_manager op_manager_{state_};
    asset::model_library model_library_;
    file_service file_service_;
    clip_service clip_service_;
    playback_service playback_service_;
    keyframe_service keyframe_service_;
    fsm_service fsm_service_;
    clipboard_service clipboard_service_;

    tools active_tool_ = tools::add_voxel;
    std::unordered_map<tools, std::unique_ptr<base_tool>> tools_;

    menu_bar menu_bar_;
    breadcrumb_bar breadcrumb_bar_;
    tool_panel tool_panel_;
    selection_panel selection_panel_;
    paste_panel paste_panel_;
    gizmo_panel gizmo_panel_;
    voxel_palette_panel voxel_palette_panel_;
    entity_properties_panel entity_properties_panel_;
    socket_panel socket_panel_;
    keyframe_properties_panel keyframe_properties_panel_;
    entity_tree_panel entity_tree_panel_;
    timeline_panel timeline_panel_;
    fsm_panel fsm_panel_;

    startup_modal startup_modal_;
    new_file_modal new_file_modal_;
    open_file_modal open_file_modal_;
    save_as_modal save_as_modal_;
    unsaved_changes_modal unsaved_changes_modal_;
    rename_model_modal rename_model_modal_;
    add_machine_modal add_machine_modal_;
    shortcuts_modal shortcuts_modal_;
    create_clip_modal create_clip_modal_;
    open_clip_modal open_clip_modal_;
};

}  // namespace vw::sculptor
