export module vw.sculptor:operations;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;

export namespace vw::sculptor {

class operation_manager;

class base_operation {
public:
    base_operation()          = default;
    virtual ~base_operation() = default;

    base_operation(base_operation&&)                    = default;
    auto operator=(base_operation&&) -> base_operation& = default;

    base_operation(const base_operation&)                    = delete;
    auto operator=(const base_operation&) -> base_operation& = delete;

    virtual auto execute() -> void = 0;
    virtual auto undo() -> void    = 0;

    [[nodiscard]] auto get_context() const -> const std::vector<edit_context>& {
        return context_;
    }

private:
    friend class operation_manager;

    std::vector<edit_context> context_;
};

class composite_operation final : public base_operation {
public:
    explicit composite_operation(std::vector<std::unique_ptr<base_operation>> parts);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    std::vector<std::unique_ptr<base_operation>> parts_;
};

auto replace_volume(
    gfx::engine& engine, std::shared_ptr<asset::model> current, std::shared_ptr<asset::model> next
) -> void;

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_animation_target_params {
    std::string entity_name;
    std::string target_name;
};

class add_animation_target_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_animation_target_operation(
        engine_type& engine, app_state& state, const add_animation_target_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_animation_target_params params_;

    [[nodiscard]] auto find_animation_root_(ecs::entity ent) const -> ecs::entity;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_keyframe_params {
    std::string clip_name;
    std::string track_name;
    asset::animation_property property;
    keyframe_value keyframe;
};

class add_keyframe_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_keyframe_operation(
        engine_type& engine, app_state& state, const add_keyframe_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_keyframe_params params_;
    bool created_channel_ = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_model_component_params {
    std::string name;
    vec3i size{8, 8, 8};
    std::optional<voxel> fill;
};

class add_model_component_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_model_component_operation(
        engine_type& engine, app_state& state, const add_model_component_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_model_component_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct attach_model_params {
    std::string name;
    asset::asset_ref source;
};

class attach_model_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    attach_model_operation(
        engine_type& engine, app_state& state, asset::model_library& library,
        attach_model_params params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    attach_model_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_socket_component_params {
    std::string name;
};

class add_socket_component_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_socket_component_operation(
        engine_type& engine, app_state& state, const add_socket_component_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_socket_component_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_socket_params {
    std::string entity_name;
    std::string socket_name;
    vec3f position{};
    quat rotation{};
    vec3f scale{1.0F, 1.0F, 1.0F};
};

class add_socket_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_socket_operation(engine_type& engine, app_state& st, const add_socket_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_socket_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_track_params {
    std::string clip_name;
    std::string track_name;
    std::optional<asset::animation_property> property;
    std::optional<keyframe_value> keyframe;
};

class add_track_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_track_operation(engine_type& engine, app_state& state, const add_track_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_track_params params_;
    bool added_target_component_ = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_voxel_params {
    std::string name;
    vec3i position;
    voxel new_voxel;
};

class add_voxel_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_voxel_operation(engine_type& eng, app_state& st, const add_voxel_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_voxel_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct close_clip_params {
    std::string name;
};

class close_clip_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    close_clip_operation(engine_type& engine, app_state& state, const close_clip_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    close_clip_params params_;
    std::shared_ptr<asset::animation_clip> saved_clip_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct create_clip_params {
    std::string name;
};

class create_clip_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    create_clip_operation(engine_type& engine, app_state& state, const create_clip_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    create_clip_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct create_entity_params {
    std::string name;
    std::string parent_name;
};

class create_entity_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    create_entity_operation(
        engine_type& engine, app_state& state, const create_entity_params& params = {}
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;

    create_entity_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct move_entity_params {
    std::string name;
    std::string parent_name;

    std::size_t index_among_other_children = 0;
};

class move_entity_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    move_entity_operation(engine_type& engine, app_state& state, const move_entity_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto place_(ecs::entity parent, std::size_t index, const transform& local) -> void;

    engine_type* engine_;
    app_state* state_;
    move_entity_params params_;

    ecs::entity previous_parent_;
    std::size_t previous_index_ = 0;
    transform previous_local_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct delete_entity_params {
    std::string name;
};

class delete_entity_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    delete_entity_operation(
        engine_type& engine, app_state& state, asset::model_library& library,
        const delete_entity_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    struct saved_node {
        std::string name;
        transform placement;
        std::shared_ptr<asset::model> volume;
        asset::asset_ref source;
    };

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    delete_entity_params params_;

    asset::vox_prefab_data snapshot_;
    std::vector<saved_node> saved_nodes_;
    std::string parent_name_;
    std::size_t index_among_siblings_ = 0;
    bool was_root_                    = false;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct expand_model_params {
    std::string name;
    vec3i dir{0,0,1};
};

class expand_model_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    expand_model_operation(engine_type& eng, app_state& st, const expand_model_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    expand_model_params params_;
    vec3i previous_size_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct add_variant_slot_params {
    std::string name;
};

class add_variant_slot_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    add_variant_slot_operation(
        engine_type& eng, app_state& st, const add_variant_slot_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    add_variant_slot_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_variant_slot_params {
    std::string name;
};

class remove_variant_slot_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_variant_slot_operation(
        engine_type& eng, app_state& st, asset::model_library& library,
        const remove_variant_slot_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    remove_variant_slot_params params_;

    std::string saved_name_;
    std::vector<asset::asset_ref> saved_;
    std::size_t saved_index_ = 0;
    std::vector<std::string> saved_targets_;
    std::vector<std::string> saved_sockets_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_variant_candidates_params {
    std::string name;
    std::vector<asset::asset_ref> candidates;
};

class set_variant_candidates_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_variant_candidates_operation(
        engine_type& eng, app_state& st, asset::model_library& library,
        const set_variant_candidates_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(ecs::entity ent, std::size_t index) const -> void;

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    set_variant_candidates_params params_;

    std::vector<asset::asset_ref> previous_;
    std::size_t previous_index_ = 0;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct select_variant_params {
    std::string name;
    std::size_t index = 0;
};

class select_variant_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    select_variant_operation(
        engine_type& eng, app_state& st, asset::model_library& library,
        const select_variant_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;

    asset::model_library* library_;

    select_variant_params params_;
    std::size_t previous_ = 0;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct trim_model_params {
    std::string name;
};

class trim_model_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    trim_model_operation(engine_type& eng, app_state& st, const trim_model_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    trim_model_params params_;

    std::shared_ptr<asset::model> previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct reorient_model_params {
    std::string name;
    asset::voxel_orientation how;
};

class reorient_model_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    reorient_model_operation(
        engine_type& eng, app_state& st, const reorient_model_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    reorient_model_params params_;

    std::shared_ptr<asset::model> previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct erase_voxels_params {
    std::string name;
    asset::voxel_bounds region;
};

class erase_voxels_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    erase_voxels_operation(engine_type& eng, app_state& st, const erase_voxels_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    erase_voxels_params params_;

    std::shared_ptr<asset::model> previous_;
};

struct fill_voxels_params {
    std::string name;
    asset::voxel_bounds region;
    voxel value;
    asset::fill_scope scope = asset::fill_scope::every_cell;
};

class fill_voxels_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    fill_voxels_operation(engine_type& eng, app_state& st, const fill_voxels_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    fill_voxels_params params_;

    std::shared_ptr<asset::model> previous_;
};

struct paste_voxels_params {
    std::string name;
    asset::voxel_clip clip;
    vec3i origin;
    asset::paste_mode mode = asset::paste_mode::keep_air;
};

class paste_voxels_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    paste_voxels_operation(engine_type& eng, app_state& st, paste_voxels_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    paste_voxels_params params_;

    std::shared_ptr<asset::model> previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct modify_keyframe_params {
    std::string clip_name;
    std::string track_name;
    asset::animation_property property;
    keyframe_value old_keyframe;
    keyframe_value new_keyframe;
};

class modify_keyframe_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    modify_keyframe_operation(
        engine_type& engine, app_state& state, const modify_keyframe_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply(const keyframe_value& replacement) const -> void;

    engine_type* engine_;
    app_state* state_;
    modify_keyframe_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class operation_manager final {
public:
    explicit operation_manager(app_state& st);

    operation_manager(const operation_manager&)                    = delete;
    auto operator=(const operation_manager&) -> operation_manager& = delete;

    auto execute(std::unique_ptr<base_operation> op) -> void;

    auto clear() -> void;

    [[nodiscard]] auto is_undo_empty() const -> bool;
    auto undo() -> void;

    [[nodiscard]] auto is_redo_empty() const -> bool;
    auto redo() -> void;

private:
    app_state* state_;

    std::deque<std::unique_ptr<base_operation>> undo_;
    std::deque<std::unique_ptr<base_operation>> redo_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct paint_voxel_params {
    std::string name;
    vec3i position;
    voxel new_voxel;
};

class paint_voxel_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    paint_voxel_operation(engine_type& eng, app_state& st, const paint_voxel_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    paint_voxel_params params_;
    voxel previous_voxel_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_animation_target_params {
    std::string entity_name;
};

class remove_animation_target_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_animation_target_operation(
        engine_type& engine, app_state& state, const remove_animation_target_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_animation_target_params params_;

    std::string saved_target_name_;

    [[nodiscard]] auto find_animation_root_(ecs::entity ent) const -> ecs::entity;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_keyframe_params {
    std::string clip_name;
    std::string track_name;
    asset::animation_property property;
    keyframe_value keyframe;
};

class remove_keyframe_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_keyframe_operation(
        engine_type& engine, app_state& state, const remove_keyframe_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_keyframe_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct duplicate_entity_params {
    std::string name;
    std::string parent_name;
    std::unordered_map<std::string, std::string> names;
    std::optional<asset::voxel_axis> mirror;
};

class duplicate_entity_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    duplicate_entity_operation(
        engine_type& engine, app_state& state, asset::model_library& library,
        const duplicate_entity_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    struct copied_node {
        std::string name;
        transform placement;
        std::shared_ptr<asset::model> volume;
        std::vector<ecs::socket_point> sockets;
    };

    engine_type* engine_;
    app_state* state_;
    asset::model_library* library_;
    duplicate_entity_params params_;

    std::vector<std::string> made_names_;
    std::string selected_before_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct fork_volume_params {
    std::string name;
    asset::asset_ref source;
};

class fork_volume_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    fork_volume_operation(engine_type& eng, app_state& st, const fork_volume_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto put_(std::shared_ptr<asset::model> volume, const asset::asset_ref& source) const -> void;

    engine_type* engine_;
    app_state* state_;
    fork_volume_params params_;

    std::shared_ptr<asset::model> shared_before_;
    asset::asset_ref source_before_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct rename_entity_params {
    std::string name;
    std::string new_name;
};

class rename_entity_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    rename_entity_operation(engine_type& engine, app_state& st, const rename_entity_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto rename_(const std::string& from, const std::string& to) const -> void;

    engine_type* engine_;
    app_state* state_;
    rename_entity_params params_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_rig_params {
    std::string rig_name;
};

class set_rig_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_rig_operation(engine_type& engine, app_state& st, const set_rig_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(const std::string& rig_name) const -> void;

    engine_type* engine_;
    app_state* state_;
    set_rig_params params_;
    std::string previous_rig_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_pivot_params {
    std::string name;
    vec3f new_pivot;
};

class set_pivot_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_pivot_operation(engine_type& engine, app_state& st, const set_pivot_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    set_pivot_params params_;
    vec3f previous_pivot_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_model_component_params {
    std::string name;
};

class remove_model_component_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_model_component_operation(
        engine_type& engine, app_state& state, const remove_model_component_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_model_component_params params_;

    std::shared_ptr<asset::model> saved_model_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_socket_component_params {
    std::string name;
};

class remove_socket_component_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_socket_component_operation(
        engine_type& engine, app_state& state, const remove_socket_component_params& params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    struct saved_socket {
        std::string name;
        vec3f position;
        quat rotation;
        vec3f scale{1.0F, 1.0F, 1.0F};
    };

    engine_type* engine_;
    app_state* state_;
    remove_socket_component_params params_;

    std::vector<saved_socket> saved_sockets_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_socket_params {
    std::string entity_name;
    std::string socket_name;
};

class remove_socket_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_socket_operation(engine_type& engine, app_state& st, const remove_socket_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_socket_params params_;
    vec3f saved_position_;
    quat saved_rotation_;
    vec3f saved_scale_{1.0F, 1.0F, 1.0F};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_track_params {
    std::string clip_name;
    std::string track_name;
};

class remove_track_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_track_operation(engine_type& eng, app_state& state, remove_track_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_track_params params_;
    std::optional<asset::animation_track> saved_track_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct remove_voxel_params {
    std::string name;
    vec3i position;
};

class remove_voxel_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_voxel_operation(engine_type& eng, app_state& st, const remove_voxel_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_voxel_params params_;
    voxel previous_voxel_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_socket_transform_params {
    std::string entity_name;
    std::string socket_name;
    vec3f position;
    quat rotation;
    vec3f scale{1.0F, 1.0F, 1.0F};
};

class set_socket_transform_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_socket_transform_operation(engine_type& engine, app_state& st,
                                   const set_socket_transform_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto update_attached_(const vec3f& position, const quat& rotation, const vec3f& scale) -> void;
    auto update_preview_(const vec3f& position, const quat& rotation, const vec3f& scale) -> void;

    engine_type* engine_;
    app_state* state_;
    set_socket_transform_params params_;
    vec3f previous_position_;
    quat previous_rotation_;
    vec3f previous_scale_{1.0F, 1.0F, 1.0F};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_transform_params {
    std::string name;
    transform new_transform;
};

class set_transform_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_transform_operation(engine_type& engine, app_state& st, const set_transform_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    set_transform_params params_;
    transform previous_transform_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_fsm_params {
    asset::voxf_data before;
    asset::voxf_data after;
    asset::asset_ref machine;
};

class set_fsm_operation final : public base_operation {
public:
    set_fsm_operation(app_state& st, set_fsm_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    [[nodiscard]] auto edits_open_machine_() const -> bool;

    app_state* state_;
    set_fsm_params params_;
};

struct set_machines_params {
    std::vector<asset::asset_ref> machines;
};

class set_machines_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_machines_operation(engine_type& engine, app_state& st, set_machines_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(const std::vector<asset::asset_ref>& machines) -> void;

    engine_type* engine_;
    app_state* state_;
    set_machines_params params_;
    std::vector<asset::asset_ref> previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_structure_params {
    std::string name;
    std::string type;
    std::vector<std::string> races;
    uint8 tier = 0;
    ecs::structure_size size = ecs::structure_size::unspecified;
};

class set_structure_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_structure_operation(engine_type& engine, app_state& st, set_structure_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(const set_structure_params& params) -> void;

    engine_type* engine_;
    app_state* state_;
    set_structure_params params_;
    set_structure_params previous_;
    bool existed_before_ = false;
};

struct remove_structure_params {
    std::string name;
};

class remove_structure_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    remove_structure_operation(
        engine_type& engine, app_state& st, remove_structure_params params
    );

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    remove_structure_params params_;
    std::optional<set_structure_params> removed_;
};

enum class point_kind : uint8 { furniture, connection };

struct set_point_params {
    std::string name;
    point_kind kind = point_kind::furniture;

    std::string tag;
    bool present = true;
};

class set_point_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_point_operation(engine_type& engine, app_state& st, set_point_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(const set_point_params& params) -> void;

    engine_type* engine_;
    app_state* state_;
    set_point_params params_;
    set_point_params previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct edit_voxels_params {
    std::string name;
    std::vector<asset::voxel_edit> edits;
};

class edit_voxels_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    edit_voxels_operation(engine_type& eng, app_state& st, edit_voxels_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    edit_voxels_params params_;
    std::shared_ptr<asset::model> previous_;
};

struct resize_volume_params {
    std::string name;
    vec3i grown_at_min;
    vec3i grown_at_max;
};

class resize_volume_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    resize_volume_operation(engine_type& eng, app_state& st, const resize_volume_params& params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    engine_type* engine_;
    app_state* state_;
    resize_volume_params params_;
    std::shared_ptr<asset::model> previous_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

struct set_clip_tracks_params {
    std::string clip_name;
    std::vector<asset::animation_track> tracks;
};

class set_clip_tracks_operation final : public base_operation {
public:
    using engine_type = gfx::engine;

    set_clip_tracks_operation(engine_type& eng, app_state& st, set_clip_tracks_params params);

    auto execute() -> void override;
    auto undo() -> void override;

private:
    auto apply_(const std::vector<asset::animation_track>& tracks) -> void;

    engine_type* engine_;
    app_state* state_;
    set_clip_tracks_params params_;
    std::vector<asset::animation_track> previous_;
};

}  // namespace vw::sculptor
