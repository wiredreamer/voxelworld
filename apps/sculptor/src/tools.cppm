export module vw.sculptor:tools;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;
import :state;
import :operations;

// ---- from src/tools/base_tool.h
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

// ---- from src/tools/add_voxel_tool.h
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

// ---- from src/tools/color_picker_tool.h
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

// ---- from src/tools/dummy_tool.h
export namespace vw::sculptor {

class dummy_tool final : public base_tool {
public:
    auto render(
        [[maybe_unused]] float delta_time
    ) -> void override{}

    auto on_key_press(
        [[maybe_unused]] const plat::key_press_event& ev
    ) -> void override{}

    auto on_mouse_move(
        [[maybe_unused]] const plat::mouse_move_event& ev
    ) -> void override{}

    auto on_mouse_press(
        [[maybe_unused]] const plat::mouse_press_event& ev
    ) -> void override{}

    auto on_mouse_release(
        [[maybe_unused]] const plat::mouse_release_event& ev
    ) -> void override{}

    auto on_activate() -> void override{}
};

}  // namespace vw::sculptor

// ---- from src/tools/paint_tool.h
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

// ---- from src/tools/remove_voxel_tool.h
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

// ---- from src/tools/gizmo.h
export namespace vw::sculptor {

enum class gizmo_mode : uint8 { translate, rotate, scale };

enum class gizmo_axis : uint8 { none, x, y, z };

// Манипулятор выбранной сущности. Не инструмент: он живёт поверх выбора и
// обязан работать и там, где инструменты запрещены, — в анимационном режиме
// поза узла правится именно им.
class gizmo final {
public:
    using engine_type = gfx::engine;

    gizmo(engine_type& eng, app_state& st, operation_manager& op_manager);

    auto render(ecs::entity ent) -> void;

    auto on_mouse_move(ecs::entity ent) -> void;

    // Отвечает, взял ли манипулятор нажатие себе: если взял, выбор сущности под
    // курсором меняться не должен — иначе клик по ручке перекидывал бы выбор на
    // то, что за ней.
    auto on_mouse_press(ecs::entity ent) -> bool;
    auto on_mouse_release() -> void;

    [[nodiscard]] auto is_dragging() const -> bool {
        return dragging_;
    }

    [[nodiscard]] auto get_mode() const -> gizmo_mode {
        return mode_;
    }

    auto set_mode(gizmo_mode mode) -> void;

private:
    // Оси, вдоль которых ходит ручка, и точка, вокруг которой всё вращается.
    // Оси берутся у родителя: позиция и поворот узла заданы относительно него,
    // и дельта по мировой оси легла бы в них криво.
    struct frame {
        vec3f pivot;
        std::array<vec3f, 3> axes;
        float32 scale;
    };

    [[nodiscard]] auto build_frame_(ecs::entity ent) const -> std::optional<frame>;
    [[nodiscard]] auto pick_(const frame& fr) const -> gizmo_axis;
    [[nodiscard]] auto snap_enabled_() const -> bool;

    auto apply_translate_(ecs::entity ent, const frame& fr) -> void;
    auto apply_rotate_(ecs::entity ent, const frame& fr) -> void;
    auto apply_scale_(ecs::entity ent, const frame& fr) -> void;

    auto draw_arrow_(const frame& fr, const vec3f& axis, color col) -> void;
    auto draw_torus_(const frame& fr, const vec3f& axis, color col) -> void;
    auto draw_handle_box_(const vec3f& center, float32 half, color col) -> void;

    engine_type* engine_;
    app_state* state_;
    operation_manager* op_manager_;

    gizmo_mode mode_    = gizmo_mode::translate;
    gizmo_axis hovered_ = gizmo_axis::none;
    gizmo_axis active_  = gizmo_axis::none;

    bool dragging_ = false;

    // Снимок на начало жеста: правка идёт от него, а не от предыдущего кадра,
    // иначе ошибка копится, а undo обязан вернуть ровно исходное состояние.
    transform start_transform_;
    float32 start_offset_ = 0.0F;
    float32 start_angle_  = 0.0F;

    // Система отсчёта тоже замораживается. Считать от свежей нельзя: сдвинув
    // узел, она сама уезжает вслед за ним, и следующий кадр меряет смещение уже
    // от нового места — узел начинает дёргаться.
    frame drag_frame_{};
};

}  // namespace vw::sculptor

// ---- from src/tools/select_entity_tool.h
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
