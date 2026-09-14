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

// ---- from src/services/clip_service.h
export namespace vw::sculptor {

class clip_service final {
public:
    using engine_type = gfx::engine;

    clip_service(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto save_clip(const std::string& clip_name) const -> bool;
    auto save_clip_as(const std::string& clip_name, const std::string& new_name) const -> bool;
    auto save_all_clips() const -> void;
    auto load_clip(const std::string& filename) const -> bool;
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

// ---- from src/services/file_service.h
export namespace vw::sculptor {

class file_service final {
public:
    using engine_type = gfx::engine;

    file_service(engine_type& eng, app_state& state, asset::model_library& library);

    auto save() -> bool;
    auto save_as(const std::filesystem::path& filepath) -> bool;

private:
    auto write_(const asset::asset_ref& prefab_ref) -> bool;

    // Узел, объём которого ещё ни разу не лежал в файле, получает имя здесь:
    // куда ляжет .voxm, знает только запись — она одна знает путь префаба.
    auto assign_missing_refs_(const asset::asset_ref& prefab_ref) -> void;
    auto write_dirty_models_() -> void;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
};

}  // namespace vw::sculptor

// ---- from src/services/keyframe_service.h
export namespace vw::sculptor {

class keyframe_service final {
public:
    using engine_type = gfx::engine;

    keyframe_service(engine_type& eng, app_state& state, operation_manager& op_manager);

    auto add_keyframe() -> void;
    auto delete_keyframe() -> void;

private:
    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;
};

}  // namespace vw::sculptor

// ---- from src/services/playback_service.h
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
