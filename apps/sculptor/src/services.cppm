export module vw.sculptor:services;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;
import :operations;

export namespace vw::sculptor {

enum class clip_load_status : uint8 { loaded, file_error, rig_mismatch };

struct clip_load_report {
    clip_load_status status = clip_load_status::file_error;
    ecs::rig_report rig;
};

class clip_service final {
public:
    using engine_type = gfx::engine;

    clip_service(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto save_clip(const std::string& clip_name) const -> bool;
    auto save_clip_as(const std::string& clip_name, const std::string& new_name) const -> bool;
    auto save_all_clips() const -> void;
    auto load_clip(const std::string& filename, bool ignore_rig = false) const
        -> clip_load_report;
    auto close_clip(const std::string& clip_name) const -> void;

    auto enter_animation_mode() -> void;
    auto exit_animation_mode() -> void;
    auto force_exit_animation_mode() -> void;

    auto save_transforms() -> void;
    auto restore_transforms() -> void;
    auto reset_all() -> void;

    auto stop_layer_for_clip(const std::string& clip_name) -> void;
    auto stop_all_layers() -> void;

    using outcome = std::expected<void, std::string>;

    [[nodiscard]] auto open_clips() const -> std::vector<std::string>;
    [[nodiscard]] auto find(std::string_view name) const
        -> std::expected<std::shared_ptr<asset::animation_clip>, std::string>;

    auto create(std::string_view name, bool overwrite) -> outcome;
    auto open(std::string_view name, bool ignore_rig) -> outcome;
    auto select(std::string_view name) -> outcome;
    auto save(std::string_view name) -> outcome;
    auto close(std::string_view name, bool discard_unsaved) -> outcome;
    auto set_tracks(std::string_view name, std::vector<asset::animation_track> tracks) -> outcome;
    auto show_pose(std::string_view name, float32 time) -> outcome;

private:
    [[nodiscard]] auto root_() const -> std::expected<ecs::entity, std::string>;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

enum class rename_model_error : uint8 { invalid_name, name_in_use, file_exists, write_failed };

enum class prefab_error : uint8 { invalid_name, already_exists, not_found, read_failed, write_failed };

[[nodiscard]] auto prefab_filename(std::string_view name) -> std::optional<std::string>;
[[nodiscard]] auto list_prefabs() -> std::vector<std::string>;

class file_service final {
public:
    using engine_type = gfx::engine;

    file_service(
        engine_type& eng, app_state& state, asset::model_library& library,
        operation_manager& op_manager
    );

    auto create(std::string_view name, bool overwrite) -> std::expected<void, prefab_error>;
    auto open(std::string_view name) -> std::expected<void, prefab_error>;

    auto save() -> bool;
    auto save_as(std::string_view filename) -> bool;
    auto close() -> void;

    auto rename_model(const asset::asset_ref& ref, std::string_view stem, bool overwrite)
        -> std::expected<void, rename_model_error>;

    auto collect_dirty_models() -> void;

private:
    using model_moves = std::unordered_map<asset::asset_ref, asset::asset_ref>;

    auto write_(const asset::asset_ref& prefab_ref) -> bool;
    auto reset_document_() -> void;

    [[nodiscard]] auto is_model_referenced_(const asset::asset_ref& ref) const -> bool;
    [[nodiscard]] auto write_model_copy_(const asset::asset_ref& from, const asset::asset_ref& to)
        -> bool;

    [[nodiscard]] auto plan_model_moves_(
        const asset::asset_ref& from_prefab, const asset::asset_ref& to_prefab
    ) const -> model_moves;
    auto copy_detached_models_(const model_moves& moves) -> void;
    auto retarget_models_(const model_moves& moves) -> void;

    auto assign_missing_refs_(const asset::asset_ref& prefab_ref) -> void;
    auto write_dirty_models_() -> void;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class clipboard_service final {
public:
    using engine_type = gfx::engine;

    clipboard_service(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto select_all() -> void;
    auto deselect() -> void;

    auto copy() -> void;
    auto cut() -> void;
    auto erase() -> void;
    auto fill(asset::fill_scope scope) -> void;

    auto begin_paste() -> void;
    auto reorient_paste(const asset::voxel_orientation& how) -> void;
    auto apply_paste() -> void;
    auto cancel_paste() -> void;

    auto sync() -> void;

private:
    [[nodiscard]] auto volume_entity_(const std::string& node_name) const -> ecs::entity;
    [[nodiscard]] auto edited_volume_() const -> std::shared_ptr<asset::model>;

    auto drop_stale_selection_() -> void;
    auto leave_paste_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class keyframe_service final {
public:
    using engine_type = gfx::engine;

    keyframe_service(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto delete_keyframe() -> void;

    auto record_pose(bool all_channels) -> void;

    auto step_to_key(bool forward) -> void;

    [[nodiscard]] auto has_key_at_cursor() const -> bool;

    auto move_keyframe(
        const std::string& track_name, asset::animation_property property, uint32 keyframe_id,
        float32 time
    ) -> void;

    auto set_keyframe(
        const std::string& track_name, asset::animation_property property,
        const keyframe_value& keyframe
    ) -> void;

    auto modify_keyframe(
        const std::string& track_name, asset::animation_property property,
        const keyframe_value& old_keyframe, const keyframe_value& new_keyframe
    ) -> void;

    auto preview_keyframe(
        const std::string& track_name, asset::animation_property property,
        const keyframe_value& keyframe
    ) -> void;

private:
    [[nodiscard]] auto place_keyframe_(
        const std::string& track_name, asset::animation_property property,
        const keyframe_value& keyframe, const std::optional<keyframe_value>& replaced,
        bool track_exists
    ) const -> std::vector<std::unique_ptr<base_operation>>;

    auto execute_parts_(std::vector<std::unique_ptr<base_operation>> parts) -> void;

    [[nodiscard]] auto key_at_(
        const std::string& track_name, asset::animation_property property, float32 time
    ) const -> std::optional<keyframe_value>;

    [[nodiscard]] auto pose_value_(
        ecs::entity ent, asset::animation_property property, float32 time
    ) const -> keyframe_value;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class playback_service final {
public:
    using engine_type = gfx::engine;

    playback_service(engine_type& eng, app_state& state);

    auto toggle_playback() const -> void;
    auto stop_playback() const -> void;

private:
    engine_type* engine_;
    app_state* state_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class fsm_service final {
public:
    using engine_type = gfx::engine;

    fsm_service(engine_type& eng, app_state& state, asset::model_library& library);

    auto enter(std::size_t layer) -> bool;

    auto save() -> bool;
    auto leave() -> void;

    auto create(std::string_view filename) -> std::optional<asset::asset_ref>;

    [[nodiscard]] auto machines() const -> std::vector<asset::asset_ref>;

private:
    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct volume_spec {
    std::optional<vec3i> blank_size;
    asset::asset_ref source;
};

struct socket_spec {
    std::string name;
    vec3f position{};
    quat rotation{};
    vec3f scale{1.0F, 1.0F, 1.0F};
};

struct variant_spec {
    std::vector<asset::asset_ref> candidates;
    std::size_t selected = 0;
};

struct structure_spec {
    std::string type;
    std::vector<std::string> races;
    uint8 tier               = 0;
    ecs::structure_size size = ecs::structure_size::unspecified;
};

template <typename T>
struct component_change {
    bool requested = false;
    std::optional<T> value;
};

struct component_changes {
    component_change<volume_spec> volume;
    component_change<std::string> anim_target;
    component_change<std::vector<socket_spec>> sockets;
    component_change<variant_spec> variant;
    component_change<structure_spec> structure;
    component_change<std::string> furniture;
    component_change<std::string> connection;
};

struct node_spec {
    std::string name;
    std::string parent;
    transform placement;
    component_changes components;
};

inline constexpr int32 max_volume_side = 256;

class node_service final {
public:
    using engine_type = gfx::engine;
    using outcome     = std::expected<void, std::string>;

    node_service(
        engine_type& eng, app_state& state, asset::model_library& library,
        operation_manager& op_manager, clip_service& clips
    );

    [[nodiscard]] auto names() const -> std::vector<std::string>;

    auto create(const node_spec& spec) -> outcome;
    auto remove(std::string_view name) -> outcome;
    auto reparent(std::string_view name, std::string_view parent, std::optional<std::size_t> index)
        -> outcome;
    auto set_transform(std::string_view name, const transform& placement) -> outcome;
    auto set_components(std::string_view name, const component_changes& changes) -> outcome;
    auto set_rig(std::string_view rig) -> outcome;

private:
    using parts = std::vector<std::unique_ptr<base_operation>>;

    [[nodiscard]] auto require_document_() -> outcome;
    [[nodiscard]] auto find_(std::string_view name) const
        -> std::expected<ecs::entity, std::string>;
    auto run_(parts steps) -> void;
    [[nodiscard]] auto verify_variant_(const std::string& name, const component_changes& changes)
        -> outcome;

    [[nodiscard]] auto plan_components_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_volume_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_anim_target_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_sockets_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_variant_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_structure_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;
    [[nodiscard]] auto plan_points_(
        const std::string& name, std::optional<ecs::entity> existing,
        const component_changes& changes, parts& steps
    ) const -> outcome;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    operation_manager* op_manager_;
    clip_service* clips_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

[[nodiscard]] auto list_node_names(const app_state& state) -> std::vector<std::string>;

auto leave_edit_contexts(app_state& state, clip_service& clips) -> void;

class volume_service final {
public:
    using engine_type = gfx::engine;
    using outcome     = std::expected<void, std::string>;
    using volume      = std::shared_ptr<asset::model>;

    volume_service(
        engine_type& eng, app_state& state, operation_manager& op_manager, file_service& files,
        clip_service& clips
    );

    [[nodiscard]] auto find(std::string_view node) const -> std::expected<volume, std::string>;
    [[nodiscard]] auto holders(const volume& held) const -> std::vector<std::string>;

    auto write(std::string_view node, std::vector<asset::voxel_edit> edits) -> outcome;
    auto resize(std::string_view node, vec3i grown_at_min, vec3i grown_at_max) -> outcome;
    auto trim(std::string_view node) -> outcome;
    auto reorient(std::string_view node, const asset::voxel_orientation& how) -> outcome;
    auto set_pivot(std::string_view node, const vec3f& pivot) -> outcome;
    auto rename(std::string_view node, std::string_view stem, bool overwrite) -> outcome;

private:
    [[nodiscard]] auto enter_(std::string_view node) -> std::expected<volume, std::string>;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    file_service* files_;
    clip_service* clips_;
};

}  // namespace vw::sculptor
