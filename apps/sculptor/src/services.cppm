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

struct clip_playback {
    float32 from = 0.0F;
    std::optional<asset::animation_loop_mode> loop;
    std::optional<float32> speed;
};

struct machine_to_run {
    asset::animation_fsm machine;
    asset::voxf_data data;
};

struct clip_playback_status {
    asset::animation_state state   = asset::animation_state::stopped;
    float32 time                   = 0.0F;
    asset::animation_loop_mode loop = asset::animation_loop_mode::once;
    float32 speed                  = 1.0F;
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
    auto retarget(std::string_view name, std::string_view from, std::string_view to) -> outcome;
    auto show_pose(std::string_view name, float32 time) -> outcome;
    auto play(std::string_view name, const clip_playback& how) -> outcome;
    auto stop(std::string_view name) -> outcome;
    [[nodiscard]] auto playback(std::string_view name) const -> clip_playback_status;

    [[nodiscard]] auto clip_for_machine(const asset::asset_ref& clip)
        -> std::shared_ptr<asset::animation_clip>;
    auto run_machines(std::vector<machine_to_run> machines) -> outcome;
    auto stop_machines() -> void;

private:
    struct clip_layer {
        ecs::entity root;
        std::shared_ptr<asset::animation_clip> clip;
        std::size_t index = 0;
    };

    [[nodiscard]] auto root_() const -> std::expected<ecs::entity, std::string>;
    [[nodiscard]] auto layer_for_(std::string_view name) -> std::expected<clip_layer, std::string>;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::unordered_map<asset::asset_ref, std::shared_ptr<asset::animation_clip>> machine_clips_;
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
    [[nodiscard]] auto free_model_ref(std::string_view stem, bool overwrite) const
        -> std::expected<asset::asset_ref, rename_model_error>;

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

struct machine_parameter {
    std::string name;
    asset::voxf_param_type type = asset::voxf_param_type::real;
    float32 value               = 0.0F;
};

struct machine_layer_status {
    asset::asset_ref machine;
    std::string state;
    std::string clip;
    asset::animation_state playback = asset::animation_state::stopped;
    float32 time                    = 0.0F;
};

struct machine_run_status {
    bool running = false;
    std::vector<machine_layer_status> layers;
    std::vector<machine_parameter> parameters;
    std::vector<std::string> triggers;
    std::string note;
};

struct machine_input {
    std::vector<std::pair<std::string, float32>> values;
    std::vector<std::string> triggers;
};

class fsm_service final {
public:
    using engine_type = gfx::engine;

    fsm_service(
        engine_type& eng, app_state& state, asset::model_library& library,
        operation_manager& op_manager, clip_service& clips
    );

    auto enter(std::size_t layer) -> bool;

    auto save() -> bool;
    auto leave() -> void;

    auto create(std::string_view filename) -> std::optional<asset::asset_ref>;

    [[nodiscard]] auto machines() const -> std::vector<asset::asset_ref>;

    using outcome = std::expected<void, std::string>;

    [[nodiscard]] static auto machine_ref(std::string_view name) -> asset::asset_ref;
    [[nodiscard]] auto machine_files() const -> std::vector<std::string>;
    [[nodiscard]] auto layer_of(const asset::asset_ref& machine) const
        -> std::optional<std::size_t>;

    [[nodiscard]] auto read(const asset::asset_ref& machine) const
        -> std::expected<asset::voxf_data, std::string>;
    auto replace(const asset::asset_ref& machine, asset::voxf_data data) -> outcome;
    auto save_open() -> outcome;
    auto create_machine(std::string_view name, bool attach)
        -> std::expected<asset::asset_ref, std::string>;
    auto set_machines(std::vector<asset::asset_ref> wanted) -> outcome;

    auto run() -> outcome;
    auto stop() -> void;
    auto drive(const machine_input& input) -> outcome;
    auto sync_run() -> void;
    [[nodiscard]] auto run_status() const -> machine_run_status;

    [[nodiscard]] auto clip_choices() const -> std::vector<std::string>;

private:
    struct run_snapshot {
        std::vector<std::string> states;
        std::vector<std::pair<std::string, float32>> values;
    };

    [[nodiscard]] auto root_() const -> std::expected<ecs::entity, std::string>;
    [[nodiscard]] auto rig_of_prefab_() const -> std::string;

    [[nodiscard]] auto snapshot_() const -> run_snapshot;
    auto start_(const run_snapshot* kept) -> outcome;

    std::vector<asset::asset_ref> running_refs_;
    std::vector<asset::voxf_param> running_params_;
    asset::voxf_data running_document_;
    std::string run_note_;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    operation_manager* op_manager_;
    clip_service* clips_;
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

struct duplicate_spec {
    std::string name;
    std::optional<std::string> parent;
    std::unordered_map<std::string, std::string> names;
    std::optional<asset::voxel_axis> mirror;
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
    auto rename(std::string_view name, std::string_view new_name) -> outcome;
    auto duplicate(const duplicate_spec& spec) -> outcome;
    auto reparent(std::string_view name, std::string_view parent, std::optional<std::size_t> index)
        -> outcome;
    [[nodiscard]] auto rest_placement(std::string_view name)
        -> std::expected<transform, std::string>;
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
[[nodiscard]] auto node_rename_problem(
    const app_state& state, std::string_view name, std::string_view new_name
) -> std::optional<std::string>;

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

    auto write(
        std::string_view node, std::vector<asset::voxel_edit> edits,
        std::optional<asset::voxel_axis> symmetry = std::nullopt
    ) -> outcome;
    auto resize(std::string_view node, vec3i grown_at_min, vec3i grown_at_max) -> outcome;
    auto trim(std::string_view node) -> outcome;
    auto reorient(std::string_view node, const asset::voxel_orientation& how) -> outcome;
    auto set_pivot(std::string_view node, const vec3f& pivot) -> outcome;
    auto rename(std::string_view node, std::string_view stem, bool overwrite) -> outcome;
    auto fork(std::string_view node, std::string_view stem, bool overwrite) -> outcome;

private:
    [[nodiscard]] auto enter_(std::string_view node) -> std::expected<volume, std::string>;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
    file_service* files_;
    clip_service* clips_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

enum class view_side : uint8 { iso, plus_x, minus_x, plus_y, minus_y, plus_z, minus_z };

struct view_direction {
    float32 yaw_degrees   = 0.0F;
    float32 pitch_degrees = 0.0F;
};

[[nodiscard]] auto direction_of(view_side side) -> view_direction;

struct view_request {
    std::optional<std::string> node;
    view_direction direction;
    std::optional<gfx::projection_kind> projection;
    std::optional<float32> distance;
    std::optional<float32> height;
};

struct view_report {
    std::string looking_at;
    vec3f target;
    float32 distance = 0.0F;
    float32 height   = 0.0F;
    float32 width    = 0.0F;
    gfx::projection_kind projection = gfx::projection_kind::perspective;
};

class view_service final {
public:
    using engine_type = gfx::engine;

    view_service(engine_type& eng, app_state& state);

    auto look(const view_request& request) -> std::expected<view_report, std::string>;
    auto look_from(view_side side) -> void;

    auto set_projection(gfx::projection_kind kind) -> void;
    auto toggle_projection() -> void;

    auto set_hidden(const std::vector<std::string>& nodes) -> std::expected<void, std::string>;
    [[nodiscard]] auto hidden() const -> std::vector<std::string>;

    [[nodiscard]] auto report() const -> view_report;

private:
    [[nodiscard]] auto framed_depth_() const -> float32;

    engine_type* engine_;
    app_state* state_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class preview_service final {
public:
    using engine_type = gfx::engine;
    using outcome     = std::expected<void, std::string>;

    preview_service(engine_type& eng, app_state& state, asset::model_library& library);

    auto show(std::string_view node, std::string_view socket, std::string_view prefab) -> outcome;
    auto hide(std::string_view node, std::string_view socket) -> outcome;

    [[nodiscard]] auto shown_in(std::string_view node, std::string_view socket) const
        -> std::optional<std::string>;

private:
    [[nodiscard]] auto socket_of_(std::string_view node, std::string_view socket) const
        -> std::expected<ecs::entity, std::string>;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

enum class asset_kind : uint8 { prefab, clip, machine };

class asset_service final {
public:
    using engine_type = gfx::engine;

    asset_service(engine_type& eng, app_state& state);

    auto remove(asset_kind kind, std::string_view name)
        -> std::expected<std::vector<std::string>, std::string>;

private:
    auto remove_prefab_(std::string_view name)
        -> std::expected<std::vector<std::string>, std::string>;
    auto remove_clip_(std::string_view name)
        -> std::expected<std::vector<std::string>, std::string>;
    auto remove_machine_(std::string_view name)
        -> std::expected<std::vector<std::string>, std::string>;

    engine_type* engine_;
    app_state* state_;
};

}  // namespace vw::sculptor
