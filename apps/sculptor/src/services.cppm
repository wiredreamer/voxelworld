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

class file_service final {
public:
    using engine_type = gfx::engine;

    file_service(engine_type& eng, app_state& state, asset::model_library& library);

    auto save() -> bool;
    auto save_as(std::string_view filename) -> bool;

    auto collect_dirty_models() -> void;

private:
    auto write_(const asset::asset_ref& prefab_ref) -> bool;

    auto assign_missing_refs_(const asset::asset_ref& prefab_ref) -> void;
    auto write_dirty_models_() -> void;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
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
