export module vw.sculptor:ui;

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
    std::string error_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class add_candidate_modal final {
public:
    using engine_type = gfx::engine;

    add_candidate_modal(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        asset::model_library& library
    );

    auto open(const std::string& entity_name) -> void;
    auto render() -> void;

private:
    auto confirm_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    bool need_open_ = false;
    std::string entity_name_;
    std::string selected_;
    std::vector<asset::asset_ref> files_;
};

}  // namespace vw::sculptor

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

export namespace vw::sculptor {

class open_clip_modal final {
public:
    using engine_type = gfx::engine;

    open_clip_modal(engine_type& eng, app_state& st, clip_service& clip_svc);

    auto open() -> void;
    auto render() -> void;

private:
    auto load_filenames_() -> void;

    [[nodiscard]] static auto describe_rig_report_(const ecs::rig_report& report) -> std::string;

    engine_type* engine_;
    app_state* state_;
    clip_service* clip_service_;

    bool need_open_ = false;
    std::vector<std::string> filenames_;
    std::string selected_;

    std::string error_;

    bool rig_mismatch_seen_ = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

enum class panel_slot : uint8 {
    left,
    bottom,
    right,
    footer,
};

auto begin_panel(
    app_state& state, panel_slot slot, std::string_view title, bool* open = nullptr,
    bool title_bar = true
) -> void;

auto end_panel(app_state& state, panel_slot slot) -> void;

auto imgui_input_text_string(std::string_view label, std::string& value, float32 label_column = 0.f)
    -> void;

auto imgui_input_int_left(std::string_view label, int* value) -> bool;

auto imgui_clamp_window_pos_to_viewport() -> void;

struct drag_edit {
    bool changed  = false;
    bool started  = false;
    bool finished = false;
};

auto imgui_drag_vec3f(std::string_view label, vec3f& vec, float label_offset = 60.f) -> drag_edit;

auto imgui_drag_vec3i(std::string_view label, vec3i& vec, float32 label_offset = 60.f) -> bool;

[[nodiscard]] auto collect_asset_refs(const std::filesystem::path& dir, std::string_view extension)
    -> std::vector<asset::asset_ref>;

}  // namespace vw::sculptor

export namespace vw::sculptor {

class voxel_palette_panel final {
public:
    using engine_type = gfx::engine;

    voxel_palette_panel(engine_type& eng, app_state& st);

    auto render(float delta_time) -> void;

private:
    auto swatch_(const voxel_type& type, int32 index_in_row) -> void;

    engine_type* engine_;
    app_state* state_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class create_entity_modal final {
public:
    using engine_type = gfx::engine;

    create_entity_modal(
        engine_type& eng, app_state& state, operation_manager& op_manager,
        asset::model_library& library
    );

    auto open() -> void;

    auto render(float delta_time) -> void;

private:
    enum class entity_role : uint8 { model, socket, point, empty };
    enum class volume_source : uint8 { blank, file };

    auto render_parent_() -> void;
    auto render_parent_option_(ecs::entity ent, std::size_t depth) -> void;
    auto render_role_() -> void;
    auto render_model_fields_() -> void;
    auto render_point_fields_() -> void;
    auto render_extras_() -> void;
    auto pick_model_file_(const asset::asset_ref& ref) -> void;

    [[nodiscard]] auto covered_by_role_(uint32 component) const -> bool;
    [[nodiscard]] auto create_entity_() -> bool;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    bool need_open_ = false;

    std::string name_;
    std::string parent_name_;

    entity_role role_     = entity_role::model;
    volume_source source_ = volume_source::blank;
    vec3i size_{8, 8, 8};

    std::vector<asset::asset_ref> model_files_;
    asset::asset_ref model_file_;
    std::string model_file_info_;

    point_kind point_kind_ = point_kind::furniture;
    std::string point_tag_;

    std::set<std::string> extras_;

    std::string error_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class create_keyframe_modal final {
public:
    using engine_type = gfx::engine;

    create_keyframe_modal(engine_type& eng, app_state& st, keyframe_service& kf_svc);

    auto open(const std::string& track_name) -> void;
    auto render(float delta_time) -> void;

private:
    auto create_keyframe() -> bool;

    engine_type* engine_;
    app_state* state_;
    keyframe_service* keyframe_service_;

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

export namespace vw::sculptor {

class delete_entity_modal final {
public:
    using engine_type = gfx::engine;

    delete_entity_modal(
        engine_type& eng, app_state& state, operation_manager& op_manager,
        asset::model_library& library
    );

    auto open(const std::string& delete_name) -> void;

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    bool need_open_ = false;

    std::string delete_name_;
};

}  // namespace vw::sculptor

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

export namespace vw::sculptor {

struct component_drawer_context {
    using engine_type = gfx::engine;

    engine_type& engine;
    app_state& state;
    operation_manager& ops;
    asset::model_library& library;

    ecs::entity ent;
    const std::string& node_name;
};

enum class drawer_scope : uint8 { prefab, volume };

struct component_drawer {
    std::string tag;
    std::string title;
    drawer_scope scope = drawer_scope::prefab;

    uint32 component = 0;

    std::function<void(const component_drawer_context&)> draw;

    std::function<std::string(const component_drawer_context&)> summary;
    std::function<void(const component_drawer_context&)> add;
    std::function<void(const component_drawer_context&)> remove;

    std::function<
        std::unique_ptr<base_operation>(gfx::engine&, app_state&, const std::string& node_name)>
        make_add;
};

class component_drawer_registry final {
public:
    component_drawer_registry();

    template <typename T>
    auto register_for(component_drawer drawer) -> void {
        drawer.component = ecs::component_id_of<T>();
        drawers_.push_back(std::move(drawer));
    }

    [[nodiscard]] auto all() const -> std::span<const component_drawer> {
        return drawers_;
    }

private:
    std::vector<component_drawer> drawers_;
};

[[nodiscard]] auto default_drawers() -> component_drawer_registry&;

}  // namespace vw::sculptor

export namespace vw::sculptor {

class edit_components_modal final {
public:
    using engine_type = gfx::engine;

    edit_components_modal(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        asset::model_library& library
    );

    auto open(const std::string& entity_name) -> void;
    auto render() -> void;

private:
    auto render_row_(const component_drawer& drawer, ecs::entity ent) -> void;

    static auto begin_row_(std::string_view label, std::string_view summary) -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    add_model_component_modal add_model_modal_;

    bool need_open_  = false;
    bool need_close_ = false;
    std::string entity_name_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class entity_properties_panel final {
public:
    using engine_type = gfx::engine;

    entity_properties_panel(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        asset::model_library& library
    );

    auto render(float delta_time) -> void;

private:
    auto render_header_(ecs::entity ent, const std::string& name, bool composable) -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    asset::model_library* library_;

    edit_components_modal components_modal_;
    add_candidate_modal candidate_modal_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class entity_tree_panel final {
public:
    using engine_type = gfx::engine;

    entity_tree_panel(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        asset::model_library& library
    );

    auto render(float delta_time) -> void;

private:
    enum class drop_place : uint8 { before, inside, after };

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    create_entity_modal creation_modal_;
    delete_entity_modal deletion_modal_;

    std::optional<move_entity_params> pending_move_;

    auto select_(const std::string& name) const -> void;

    auto render_entity_node(
        const std::string& name, const std::unordered_set<ecs::entity>& preview_entities,
        bool parent_hidden
    ) -> void;

    auto render_visibility_toggle_(const std::string& name) -> void;
    auto render_drag_and_drop_(const std::string& name) -> void;

    [[nodiscard]] auto plan_move_(
        const std::string& dragged, const std::string& target, drop_place place
    ) const -> std::optional<move_entity_params>;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class keyframe_properties_panel final {
public:
    using engine_type = gfx::engine;

    keyframe_properties_panel(
        engine_type& eng, app_state& st, operation_manager& op_manager, keyframe_service& kf_svc
    );

    auto render(float delta_time) -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    keyframe_service* keyframe_service_;

    bool dragging_                = false;
    uint32 dragged_keyframe_id_   = asset::invalid_keyframe_id;
    keyframe_value keyframe_before_drag_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class menu_bar final {
public:
    using engine_type = gfx::engine;

    menu_bar(
        engine_type& eng, app_state& state, operation_manager& op_manager,
        file_service& file_svc, clipboard_service& clipboard_svc
    );

    auto render(float delta_time) const -> void;

private:
    auto render_edit_menu_() const -> void;
    auto render_volume_menu_() const -> void;
    auto render_mcp_status_() const -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    file_service* file_service_;
    clipboard_service* clipboard_service_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class new_file_modal final {
public:
    using engine_type = gfx::engine;

    new_file_modal(app_state& st, file_service& file_svc);

    auto render(float delta_time) -> void;

private:
    auto render_overwrite_confirmation() -> void;
    auto render_create_form() -> void;

    auto create_file_() -> bool;

    app_state* state_;
    file_service* file_service_;

    std::string filename_;
    std::string error_;

    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class open_file_modal final {
public:
    using engine_type = gfx::engine;

    open_file_modal(app_state& st, file_service& file_svc);

    auto render(float delta_time) -> void;

private:
    auto open_file_() -> bool;

    app_state* state_;
    file_service* file_service_;

    std::string filename_;
    std::string error_;
    std::vector<std::string> existing_filenames_;
};

}  // namespace vw::sculptor

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

export namespace vw::sculptor {

class rename_model_modal final {
public:
    using engine_type = gfx::engine;

    rename_model_modal(engine_type& eng, app_state& st, file_service& file_svc);

    auto render() -> void;

private:
    auto open_(const std::string& node_name) -> void;
    auto render_overwrite_confirmation_() -> void;
    auto render_rename_form_() -> void;
    auto rename_() -> bool;

    engine_type* engine_;
    app_state* state_;
    file_service* file_service_;

    asset::asset_ref source_;
    std::string stem_;
    std::string error_;

    bool need_overwrite_confirmation_ = false;
    bool has_overwrite_confirmation_  = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class unsaved_changes_modal final {
public:
    unsaved_changes_modal(app_state& st, file_service& file_svc);

    auto render() -> void;

private:
    enum class action : uint8 { close_file, new_file, open_file };

    [[nodiscard]] auto take_request_() const -> std::optional<action>;
    auto proceed_(action confirmed) const -> void;

    app_state* state_;
    file_service* file_service_;

    action pending_ = action::close_file;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class breadcrumb_bar final {
public:
    using engine_type = gfx::engine;

    breadcrumb_bar(engine_type& eng, app_state& st, clip_service& clip_svc);

    auto render(float delta_time) -> void;

private:
    auto leave_to_(std::size_t depth) -> void;

    [[nodiscard]] auto label_of_(const edit_context& ctx) const -> std::string;

    engine_type* engine_;
    app_state* state_;
    clip_service* clip_service_;
};

}  // namespace vw::sculptor

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

export namespace vw::sculptor {

class timeline_panel final {
public:
    using engine_type = gfx::engine;

    timeline_panel(engine_type& eng, app_state& st, operation_manager& op_manager,
                   clip_service& clip_svc, keyframe_service& kf_svc);

    auto render(float delta_time) -> void;

private:
    struct row {
        std::string name;
        const asset::animation_track* track = nullptr;
        bool is_target                      = false;
    };

    [[nodiscard]] auto collect_rows_() const -> std::vector<row>;

    auto render_toolbar(float clip_duration) -> void;
    auto render_clip_controls_() -> void;
    auto render_record_controls_() -> void;
    auto render_target_hint_() -> void;
    auto render_close_confirm_popup_() const -> void;
    auto render_tracks() -> void;
    auto render_track_row(
        const row& entry,
        float track_area_width,
        float clip_duration,
        float scroll_offset
    ) -> void;
    auto render_target_row_(const row& entry, float track_area_width) -> void;

    auto update_key_drag_(float clip_duration) -> void;
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

    auto render_keyframe_context_menu_() -> void;

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
    save_clip_as_modal save_clip_as_modal_;
    layer_blend_modal layer_blend_modal_;

    bool need_close_confirm_popup_ = false;

    static constexpr std::size_t layer_count = 5;

    float zoom_percent_  = 100.f;
    float scroll_offset_ = 0.f;

    bool scrollbar_dragging_      = false;
    float scrollbar_drag_start_   = 0.f;
    float scrollbar_scroll_start_ = 0.f;

    float prev_cursor_time_     = -1.f;
    bool keyframe_clicked_      = false;
    bool need_keyframe_menu_    = false;

    float32 track_area_screen_x_ = 0.f;
    float32 track_area_width_    = 0.f;

    bool key_drag_        = false;
    bool key_drag_moved_  = false;
    uint32 drag_key_id_   = asset::invalid_keyframe_id;
    float32 drag_key_time_ = 0.f;
    std::string drag_key_track_;
    asset::animation_property drag_key_property_ = asset::animation_property::position;
    std::string prev_clip_name_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class tool_panel final {
public:
    explicit tool_panel(app_state& st);

    auto render(float delta_time) const -> void;

private:
    app_state* state_;

    auto render_tool_button(tools tool, std::string_view label) const -> void;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class selection_panel final {
public:
    selection_panel(app_state& st, clipboard_service& clipboard_svc);

    auto render(float delta_time) const -> void;

private:
    app_state* state_;
    clipboard_service* clipboard_service_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class paste_panel final {
public:
    paste_panel(app_state& st, clipboard_service& clipboard_svc);

    auto render(float delta_time) const -> void;

private:
    app_state* state_;
    clipboard_service* clipboard_service_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class gizmo_panel final {
public:
    explicit gizmo_panel(app_state& st);

    auto render(float delta_time) const -> void;

private:
    auto mode_button_(gizmo_mode mode, std::string_view label, command cmd) const -> void;

    app_state* state_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class shortcuts_modal final {
public:
    explicit shortcuts_modal(app_state& st);

    auto open() -> void;
    auto render() -> void;

private:
    app_state* state_;
    bool need_open_ = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class add_machine_modal final {
public:
    using engine_type = gfx::engine;

    add_machine_modal(
        engine_type& eng, app_state& st, operation_manager& op_manager, fsm_service& service
    );

    auto open() -> void;
    auto render() -> void;

private:
    auto confirm_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    fsm_service* service_;

    bool need_open_ = false;
    std::string selected_;
    std::string new_name_;
    std::vector<asset::asset_ref> files_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class fsm_panel final {
public:
    using engine_type = gfx::engine;

    fsm_panel(app_state& st, operation_manager& op_manager, fsm_service& service);

    auto render(float delta_time) -> void;

private:
    auto render_header_() -> void;
    auto render_params_() -> void;
    auto render_states_() -> void;
    auto render_state_(std::size_t index) -> void;
    auto render_rules_(std::vector<asset::animation_fsm::transition_rule>& rules, const char* id)
        -> void;
    auto render_rule_(
        std::vector<asset::animation_fsm::transition_rule>& rules, std::size_t index
    ) -> void;
    auto render_incoming_(const std::string& state_name) -> void;

    auto state_combo_(const char* label, std::string& target) -> bool;

    auto begin_edit_() -> void;
    auto commit_edit_() -> void;

    app_state* state_;
    operation_manager* op_manager_;
    fsm_service* service_;

    asset::voxf_data before_;
    bool editing_ = false;
};

}  // namespace vw::sculptor
