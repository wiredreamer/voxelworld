export module vw.sculptor:tools;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;
import :operations;

export namespace vw::sculptor {

class base_tool {
public:
    virtual ~base_tool() = default;

    virtual auto render(float delta_time) -> void = 0;

    virtual auto on_key_press(const plat::key_press_event& ev) -> void         = 0;
    virtual auto on_mouse_move(const plat::mouse_move_event& ev) -> void       = 0;
    virtual auto on_mouse_press(const plat::mouse_press_event& ev) -> void     = 0;
    virtual auto on_mouse_release(const plat::mouse_release_event& ev) -> void = 0;

    virtual auto on_activate() -> void = 0;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class add_voxel_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    add_voxel_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto update_hovered_voxel_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::vector<ecs::entity> ray_cast_entities_;
    vec3i hovered_voxel_ = vec3i{-1, -1, -1};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class color_picker_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    color_picker_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto update_hovered_voxel_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::vector<ecs::entity> ray_cast_entities_;
    vec3i hovered_voxel_ = vec3i{-1, -1, -1};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class paint_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    paint_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto update_hovered_voxel_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::vector<ecs::entity> ray_cast_entities_;
    vec3i hovered_voxel_ = vec3i{-1, -1, -1};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class remove_voxel_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    remove_voxel_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto update_hovered_voxel_() -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    std::vector<ecs::entity> ray_cast_entities_;
    vec3i hovered_voxel_ = vec3i{-1, -1, -1};
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

enum class gizmo_target : uint8 { node, pivot };

enum class gizmo_handle : uint8 { none, x, y, z, xy, yz, zx };

enum class gizmo_commit : uint8 { history, preview };

class gizmo final {
public:
    using engine_type = gfx::engine;

    gizmo(
        engine_type& eng, app_state& st, operation_manager& op_manager,
        gizmo_target target = gizmo_target::node,
        gizmo_commit commit = gizmo_commit::history
    );

    auto render(ecs::entity ent) -> void;

    auto on_mouse_move(ecs::entity ent) -> void;

    auto on_mouse_press(ecs::entity ent) -> bool;
    auto on_mouse_release() -> void;

    [[nodiscard]] auto is_dragging() const -> bool {
        return dragging_;
    }

    [[nodiscard]] auto is_hovered() const -> bool {
        return hovered_ != gizmo_handle::none;
    }

private:
    [[nodiscard]] auto mode_() const -> gizmo_mode;

    struct frame {
        vec3f pivot;
        std::array<vec3f, 3> axes;
        float32 scale;
    };

    [[nodiscard]] auto build_frame_(ecs::entity ent) const -> std::optional<frame>;
    [[nodiscard]] auto cursor_ray_() const -> spatial::ray;
    [[nodiscard]] auto pick_(const frame& fr) const -> gizmo_handle;
    [[nodiscard]] auto plane_faces_camera_(const frame& fr, gizmo_handle plane) const -> bool;
    [[nodiscard]] auto snap_enabled_() const -> bool;

    [[nodiscard]] auto translation_delta_(
        const frame& fr, std::optional<float32> snap_step
    ) const -> std::optional<vec3f>;

    auto apply_translate_(ecs::entity ent, const frame& fr) -> void;
    auto apply_pivot_(ecs::entity ent, const frame& fr) -> void;
    auto apply_rotate_(ecs::entity ent, const frame& fr) -> void;
    auto apply_scale_(ecs::entity ent, const frame& fr) -> void;

    auto draw_arrow_(const frame& fr, const vec3f& axis, color col) -> void;
    auto draw_plane_handle_(const frame& fr, gizmo_handle plane, color col) -> void;
    auto draw_torus_(const frame& fr, const vec3f& axis, color col) -> void;
    auto draw_handle_box_(const vec3f& center, float32 half, color col) -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    gizmo_target target_;
    gizmo_commit commit_;

    gizmo_handle hovered_ = gizmo_handle::none;
    gizmo_handle active_  = gizmo_handle::none;

    bool dragging_ = false;

    transform start_transform_;
    vec3f start_pivot_{};
    vec3f start_plane_point_{};
    float32 start_offset_ = 0.0F;
    float32 start_angle_  = 0.0F;

    frame drag_frame_{};
    gizmo_mode drag_mode_ = gizmo_mode::translate;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class move_pivot_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    move_pivot_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    [[nodiscard]] auto target_entity_() const -> ecs::entity;

    engine_type* engine_;
    app_state* state_;

    gizmo gizmo_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class pose_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    pose_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto draw_entity_box_(ecs::entity ent, color col) -> void;

    engine_type* engine_;
    app_state* state_;

    std::vector<ecs::entity> ray_cast_entities_;
    gizmo gizmo_;
};

}  // namespace vw::sculptor

export namespace vw::sculptor {

class select_entity_tool final : public base_tool {
public:
    using engine_type = gfx::engine;

    select_entity_tool(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(float delta_time) -> void override;
    auto on_key_press(const plat::key_press_event& ev) -> void override;
    auto on_mouse_move(const plat::mouse_move_event& ev) -> void override;
    auto on_mouse_press(const plat::mouse_press_event& ev) -> void override;
    auto on_mouse_release(const plat::mouse_release_event& ev) -> void override;
    auto on_activate() -> void override;

private:
    auto update_hovered_entity_() -> void;
    auto draw_entity_box_(ecs::entity ent, color col) -> void;

    engine_type* engine_;
    app_state* state_;

    std::vector<ecs::entity> ray_cast_entities_;
    ecs::entity hovered_entity_;
    gizmo gizmo_;
};

}  // namespace vw::sculptor
