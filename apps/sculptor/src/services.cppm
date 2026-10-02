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

private:
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
