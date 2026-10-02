export module vw.game:input.mapper;

import std;

import vw.core;

// см. docs/ENGINE.md#ввод
export namespace vw::game {

enum class input_action : uint8 {
    jump,
    sprint,
    attack,
    toggle_weapon,
};

inline constexpr std::size_t input_action_count =
    std::to_underlying(input_action::toggle_weapon) + 1;

using input_action_mask = uint32;

[[nodiscard]] constexpr auto action_bit(input_action action) -> input_action_mask {
    return input_action_mask{1} << std::to_underlying(action);
}

struct input_frame {
    float32 move_forward       = 0.0f;
    float32 move_right         = 0.0f;
    float32 look_yaw_degrees   = 0.0f;
    float32 look_pitch_degrees = 0.0f;
    float32 zoom_delta         = 0.0f;

    input_action_mask held    = 0;
    input_action_mask pressed = 0;

    [[nodiscard]] constexpr auto is_held(input_action action) const -> bool {
        return (held & action_bit(action)) != 0;
    }

    [[nodiscard]] constexpr auto was_pressed(input_action action) const -> bool {
        return (pressed & action_bit(action)) != 0;
    }

    [[nodiscard]] auto look_forward_flat() const -> vec3f {
        const float32 yaw = math::radians(look_yaw_degrees);
        return {std::sin(yaw), 0.0f, std::cos(yaw)};
    }

    [[nodiscard]] auto look_right_flat() const -> vec3f {
        const float32 yaw = math::radians(look_yaw_degrees);
        return {std::cos(yaw), 0.0f, -std::sin(yaw)};
    }
};

enum class move_direction : uint8 {
    forward,
    back,
    left,
    right,
};

struct key_action_binding {
    keyboard::keys key;
    input_action action;
};

struct button_action_binding {
    mouse::buttons button;
    input_action action;
};

struct key_move_binding {
    keyboard::keys key;
    move_direction direction;
};

struct input_bindings {
    std::vector<key_action_binding> key_actions;
    std::vector<button_action_binding> button_actions;
    std::vector<key_move_binding> key_moves;
};

[[nodiscard]] auto default_input_bindings() -> input_bindings;

struct input_settings {
    float32 look_degrees_per_count     = 0.1f;
    float32 look_pitch_min_degrees     = -80.0f;
    float32 look_pitch_max_degrees     = 30.0f;
    float32 initial_look_yaw_degrees   = 180.0f;
    float32 initial_look_pitch_degrees = -20.0f;
};

class input_mapper final {
public:
    explicit input_mapper(
        input_bindings bindings = default_input_bindings(), input_settings settings = {}
    );

    auto key(keyboard::keys key, bool down) -> void;
    auto button(mouse::buttons button, bool down) -> void;
    auto cursor_at(float64 x, float64 y) -> void;
    auto drop_cursor() -> void;
    auto scroll(float32 delta) -> void;
    auto release_all() -> void;

    [[nodiscard]] auto take_frame() -> input_frame;

    [[nodiscard]] auto bindings() -> input_bindings&;
    [[nodiscard]] auto settings() -> input_settings&;

private:
    input_bindings bindings_;
    input_settings settings_;

    std::array<bool, keyboard::key_count> keys_down_{};
    std::array<bool, mouse::button_count> buttons_down_{};

    input_action_mask pressed_ = 0;

    float32 look_yaw_degrees_;
    float32 look_pitch_degrees_;
    float32 zoom_delta_ = 0.0f;

    float64 cursor_x_  = 0.0;
    float64 cursor_y_  = 0.0;
    bool cursor_known_ = false;
};

}  // namespace vw::game
