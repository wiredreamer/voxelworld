export module vw.sculptor:app;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;
import :operations;
import :services;
import :tools;
import :ui;

// ---- from src/app/app.h
export namespace vw::sculptor {

class app final : public gfx::app {
public:
    using engine_type = gfx::engine;

    explicit app(engine_type& eng);
    ~app() override;

    auto render(float delta_time) -> void override;

private:
    auto handle_key_press(const plat::key_press_event& ev) -> void;
    auto handle_file_shortcuts(const plat::key_press_event& ev) -> void;
    auto handle_mouse_move(const plat::mouse_move_event& ev) -> void;
    auto handle_mouse_press(const plat::mouse_press_event& ev) -> void;
    auto handle_mouse_release(const plat::mouse_release_event& ev) -> void;

    auto handle_animation_actions_() -> void;
    auto collect_dirty_models_() -> void;
    auto prune_contexts_() -> void;
    auto update_title_() -> void;
    static auto init_asset_dirs_() -> void;

    gfx::free_camera_controller camera_controller_;
    bool camera_movement_enabled_ = false;
    bool prev_unsaved_state_      = false;
    bool prev_clip_unsaved_state_ = false;
    bool prev_animation_mode_     = false;
    std::string prev_clip_name_;
    std::string prev_filename_;

    app_state state_;
    operation_manager op_manager_{state_};
    asset::model_library model_library_;
    file_service file_service_;
    clip_service clip_service_;
    playback_service playback_service_;
    keyframe_service keyframe_service_;

    tools active_tool_ = tools::add_voxel;
    std::unordered_map<tools, std::unique_ptr<base_tool>> tools_;

    menu_bar menu_bar_;
    breadcrumb_bar breadcrumb_bar_;
    tool_panel tool_panel_;
    block_palette_panel block_palette_panel_;
    entity_properties_panel entity_properties_panel_;
    rig_panel rig_panel_;
    socket_panel socket_panel_;
    keyframe_properties_panel keyframe_properties_panel_;
    entity_tree_panel entity_tree_panel_;
    clip_manager_panel clip_manager_panel_;
    timeline_panel timeline_panel_;

    startup_modal startup_modal_;
    new_file_modal new_file_modal_;
    open_file_modal open_file_modal_;
    save_as_modal save_as_modal_;
};

}  // namespace vw::sculptor
