export module vw.sculptor:ui;

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

// ---- from src/ui/add_model_component_modal.h
export namespace vw::sculptor {

class add_model_component_modal final {
public:
    using engine_type = gfx::engine;

    add_model_component_modal(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto open(const std::string& entity_name) -> void;
    auto render() -> void;

private:
    auto confirm_() -> bool;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_ = false;
    std::string entity_name_;
    vec3i size_{8, 8, 8};
    block_category category_;
    std::string error_;
};

}  // namespace vw::sculptor

// ---- from src/ui/create_clip_modal.h
export namespace vw::sculptor {

class create_clip_modal final {
public:
    using engine_type = gfx::engine;

    create_clip_modal(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto open() -> void;
    auto render(float delta_time) -> void;

private:
    auto create_clip() -> bool;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_                  = false;
    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
    std::string name_;
    std::string error_;
};

}  // namespace vw::sculptor

// ---- from src/ui/layer_blend_modal.h
export namespace vw::sculptor {

class layer_blend_modal final {
public:
    explicit layer_blend_modal(app_state& st);

    auto open() -> void;
    auto render(float delta_time) -> void;

private:
    app_state* state_;

    bool need_open_ = false;

    float fade_in_duration_  = 0.f;
    int fade_in_interp_      = 0;
    float fade_out_duration_ = 0.f;
    int fade_out_interp_     = 0;
};

}  // namespace vw::sculptor

// ---- from src/ui/save_clip_as_modal.h
export namespace vw::sculptor {

class save_clip_as_modal final {
public:
    using engine_type = gfx::engine;

    save_clip_as_modal(engine_type& eng, app_state& st, clip_service& clip_svc);

    auto open() -> void;
    auto render(float delta_time) -> void;

private:
    auto render_overwrite_confirmation_() -> void;
    auto render_save_form_() -> void;
    auto save_clip_() -> bool;

    engine_type* engine_;
    app_state* state_;
    clip_service* clip_service_;

    std::string name_;
    std::string error_;

    bool need_open_                  = false;
    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
};

}  // namespace vw::sculptor

// ---- from src/ui/clip_manager_panel.h
export namespace vw::sculptor {

class clip_manager_panel final {
public:
    using engine_type = gfx::engine;

    clip_manager_panel(engine_type& eng, app_state& st, operation_manager& op_manager,
                       clip_service& clip_svc);

    auto render(float delta_time) -> void;

private:
    auto render_close_confirm_popup_() const -> void;
    auto render_load_popup_() -> void;
    auto load_voxa_filenames_() -> void;

    [[nodiscard]] static auto describe_rig_report_(const ecs::rig_report& report) -> std::string;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    clip_service* clip_service_;

    create_clip_modal create_modal_;
    layer_blend_modal layer_blend_modal_;
    save_clip_as_modal save_clip_as_modal_;

    bool need_load_popup_          = false;
    bool need_close_confirm_popup_ = false;
    std::vector<std::string> voxa_filenames_;
    std::string selected_load_filename_;

    std::string load_error_;
    bool rig_mismatch_seen_ = false;
};

}  // namespace vw::sculptor

// ---- from src/ui/ui_utils.h
export namespace vw::sculptor {

auto imgui_input_text_string(std::string_view label, std::string& value) -> void;

auto imgui_input_int_left(std::string_view label, int* value) -> bool;

auto imgui_clamp_window_pos_to_viewport() -> void;

auto imgui_drag_vec3f(std::string_view label, vec3f& vec, float label_offset = 60.f) -> bool;

// Набор блоков выбранной модели. Им открываются и палитра, и диалоги создания:
// набор задаётся конструктором модели и потом не меняется, так что дальше по
// сцене работают, не переключаясь.
[[nodiscard]] auto selected_model_category(gfx::engine& eng, const app_state& state)
    -> block_category;

auto imgui_block_set_combo(std::string_view label, const block_registry& registry,
                           block_category& category) -> void;

}  // namespace vw::sculptor

// ---- from src/ui/block_palette_panel.h
export namespace vw::sculptor {

class block_palette_panel final {
public:
    using engine_type = gfx::engine;

    block_palette_panel(engine_type& eng, app_state& st);

    auto render(float delta_time) -> void;

private:
    auto swatch_(const block_type& block, int32 index_in_row) -> void;

    engine_type* engine_;
    app_state* state_;
};

}  // namespace vw::sculptor

// ---- from src/ui/create_entity_modal.h
export namespace vw::sculptor {

class create_entity_modal final {
public:
    using engine_type = gfx::engine;

    create_entity_modal(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto open() -> void;

    auto render(float delta_time) -> void;

private:
    auto create_entity() -> bool;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_ = false;

    std::string name_;
    bool with_model_  = false;
    bool with_socket_ = false;
    vec3i size_{12, 12, 12};
    block_category category_;

    std::string error_;
};

}  // namespace vw::sculptor

// ---- from src/ui/create_keyframe_modal.h
export namespace vw::sculptor {

class create_keyframe_modal final {
public:
    using engine_type = gfx::engine;

    create_keyframe_modal(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto open(const std::string& track_name) -> void;
    auto render(float delta_time) -> void;

private:
    auto create_keyframe() -> bool;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_ = false;
    std::string track_name_;
    int property_index_     = 0;
    float32 time_           = 0.f;
    vec3f value_vec3f_      = {};
    vec3f value_euler_deg_  = {};
    int interp_index_       = 0;
    float32 tangent_in_     = 0.f;
    float32 tangent_out_    = 1.f;
    std::string error_;
};

}  // namespace vw::sculptor

// ---- from src/ui/delete_entity_modal.h
export namespace vw::sculptor {

class delete_entity_modal final {
public:
    using engine_type = gfx::engine;

    delete_entity_modal(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto open(const std::string& delete_name) -> void;

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_ = false;

    std::string delete_name_;
};

}  // namespace vw::sculptor

// ---- from src/ui/delete_track_modal.h
export namespace vw::sculptor {

class delete_track_modal final {
public:
    using engine_type = gfx::engine;

    delete_track_modal(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto open(const std::string& track_name) -> void;
    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    bool need_open_ = false;
    std::string track_name_;
};

}  // namespace vw::sculptor

// ---- from src/ui/entity_properties_panel.h
export namespace vw::sculptor {

class entity_properties_panel final {
public:
    using engine_type = gfx::engine;

    entity_properties_panel(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    add_model_component_modal add_model_modal_;

    mutable std::string cached_rotation_entity_;
    mutable quat cached_rotation_quat_;
    mutable vec3f cached_rotation_deg_;

    auto render_components_section() -> void;

    auto render_position() const -> void;
    auto render_rotation() const -> void;
    auto render_scale() const -> void;
};

}  // namespace vw::sculptor

// ---- from src/ui/entity_tree_panel.h
export namespace vw::sculptor {

class entity_tree_panel final {
public:
    using engine_type = gfx::engine;

    entity_tree_panel(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    create_entity_modal creation_modal_;
    delete_entity_modal deletion_modal_;

    auto render_entity_node(
        const std::string& name, const std::unordered_set<ecs::entity>& preview_entities
    ) -> void;
};

}  // namespace vw::sculptor

// ---- from src/ui/keyframe_properties_panel.h
export namespace vw::sculptor {

class keyframe_properties_panel final {
public:
    using engine_type = gfx::engine;

    keyframe_properties_panel(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

// ---- from src/ui/menu_bar.h
export namespace vw::sculptor {

class menu_bar final {
public:
    using engine_type = gfx::engine;

    menu_bar(engine_type& eng, app_state& state, operation_manager& op_manager,
             file_service& file_svc);

    auto render(float delta_time) const -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    file_service* file_service_;
};

}  // namespace vw::sculptor

// ---- from src/ui/new_file_modal.h
export namespace vw::sculptor {

class new_file_modal final {
public:
    using engine_type = gfx::engine;

    new_file_modal(engine_type& eng, app_state& st);

    auto render(float delta_time) -> void;

private:
    auto render_overwrite_confirmation() -> void;
    auto render_create_form() -> void;
    auto create_file_() -> bool;

    engine_type* engine_;
    app_state* state_;

    std::string filename_;
    std::string error_;

    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
};

}  // namespace vw::sculptor

// ---- from src/ui/open_file_modal.h
export namespace vw::sculptor {

class open_file_modal final {
public:
    using engine_type = gfx::engine;

    open_file_modal(engine_type& eng, app_state& st, asset::model_library& library);

    auto render(float delta_time) -> void;

private:
    auto load_existing_filenames_() -> void;
    auto open_file_() -> bool;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;

    std::string filename_;
    std::string error_;
    std::vector<std::string> existing_filenames_;
};

}  // namespace vw::sculptor

// ---- from src/ui/save_as_modal.h
export namespace vw::sculptor {

class save_as_modal final {
public:
    using engine_type = gfx::engine;

    save_as_modal(engine_type& eng, app_state& st, file_service& file_svc);

    auto render(float delta_time) -> void;

private:
    auto render_overwrite_confirmation() -> void;
    auto render_save_form() -> void;
    auto save_file_() -> bool;

    engine_type* engine_;
    app_state* state_;
    file_service* file_service_;

    std::string filename_;
    std::string error_;

    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
};

}  // namespace vw::sculptor

// ---- from src/ui/rig_panel.h
export namespace vw::sculptor {

// Риг документа: имя контракта плюс плоский список целей. Список нигде не
// хранится — он и есть дерево, поэтому панель собирает его заново.
class rig_panel final {
public:
    using engine_type = gfx::engine;

    rig_panel(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void;

private:
    auto render_name_(ecs::entity root) -> void;
    auto render_targets_(ecs::entity root) -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::string rig_input_;
};

}  // namespace vw::sculptor

// ---- from src/ui/socket_panel.h
export namespace vw::sculptor {

class socket_panel final {
public:
    using engine_type = gfx::engine;

    socket_panel(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        asset::model_library& library
    );

    auto render(float delta_time) -> void;

private:
    auto render_socket_(const ecs::socket_point& sp, std::string& socket_to_remove) -> void;
    auto render_add_socket_() -> void;
    auto render_add_socket_modal_() -> void;
    auto render_preview_file_list_() -> void;

    auto load_preview_(const std::string& socket_name, const std::string& filename) const -> void;
    auto unload_preview_(const std::string& key) const -> void;
    auto update_preview_transform_(
        const std::string& key, const vec3f& position, const quat& rotation, const vec3f& scale
    ) const -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    std::string new_socket_name_;
    std::string add_socket_error_;
    bool need_add_socket_modal_ = false;
    std::string pending_remove_socket_;
    bool need_preview_modal_ = false;
    std::string preview_modal_socket_;
    std::string preview_selected_file_;
    std::vector<std::string> vox_filenames_;
};

}  // namespace vw::sculptor

// ---- from src/ui/startup_modal.h
export namespace vw::sculptor {

class startup_modal final {
public:
    using engine_type = gfx::engine;

    startup_modal(engine_type& eng, app_state& state);

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;

    bool need_open_ = false;
};

}  // namespace vw::sculptor

// ---- from src/ui/timeline_panel.h
export namespace vw::sculptor {

class timeline_panel final {
public:
    using engine_type = gfx::engine;

    timeline_panel(engine_type& eng, app_state& st, operation_manager& op_manager,
                   clip_service& clip_svc, keyframe_service& kf_svc);

    auto render(float delta_time) -> void;

private:
    auto render_toolbar(float clip_duration) -> void;
    auto render_tracks() -> void;
    auto render_track_row(
        const asset::animation_track& track,
        float track_area_width,
        float clip_duration,
        float scroll_offset
    ) -> void;
    auto render_time_ruler(
        vec2f ruler_start,
        float ruler_width,
        float track_area_width,
        float clip_duration
    ) const -> void;
    auto render_scrollbar(float usable_track_width, float track_area_width, float max_scroll) -> void;
    auto render_track_context_menu(const std::string& target) -> void;
    auto render_expanded_channels(
        const asset::animation_track& track,
        const std::string& target,
        float track_area_width,
        float clip_duration,
        float scroll_offset
    ) -> void;
    auto render_keyframe_markers(
        const asset::animation_channel_variant& channel_var,
        const std::string& track_name,
        asset::animation_property prop,
        float track_width,
        float clip_duration,
        float scroll_offset
    ) -> void;
    auto render_playhead(
        float track_area_x,
        float track_width,
        float clip_duration,
        float area_top,
        float area_bottom,
        float scroll_offset
    ) const -> void;

    auto render_playback_controls(const std::shared_ptr<asset::animation_clip>& clip) -> void;
    auto render_clip_blend_controls_() const -> void;
    auto handle_play(ecs::entity root, const std::shared_ptr<asset::animation_clip>& clip) const -> void;
    auto handle_pause(ecs::entity root) const -> void;
    auto handle_stop(ecs::entity root) const -> void;
    auto try_get_root_entity() const -> std::optional<ecs::entity>;

    [[nodiscard]] auto is_current_layer_playing() const -> bool;
    [[nodiscard]] auto is_clip_on_layer() const -> bool;
    auto ensure_clip_on_layer(ecs::entity root) const -> void;
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    clip_service* clip_service_;
    keyframe_service* keyframe_service_;

    create_keyframe_modal create_kf_modal_;
    delete_track_modal delete_track_modal_;

    float zoom_percent_  = 100.f;
    float scroll_offset_ = 0.f;

    bool scrollbar_dragging_      = false;
    float scrollbar_drag_start_   = 0.f;
    float scrollbar_scroll_start_ = 0.f;

    float prev_cursor_time_     = -1.f;
    bool keyframe_clicked_      = false;
    std::string prev_clip_name_;
};

}  // namespace vw::sculptor

// ---- from src/ui/tool_panel.h
export namespace vw::sculptor {

class tool_panel final {
public:
    explicit tool_panel(app_state& st);

    auto render(float delta_time) const -> void;

private:
    app_state* state_;

    auto render_tool_button(tools tool, std::string_view label, std::string_view shortcut) const -> void;
};

}  // namespace vw::sculptor
