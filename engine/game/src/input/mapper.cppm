export module vw.game:input.mapper;

import std;

import vw.core;

// см. docs/ENGINE.md#ввод
export namespace vw::game {

enum class input_action : uint8 {
    jump,
    sprint,
    attack,
    block,
    dodge,
    ability_1,
    ability_2,
    ability_3,
    interact,
    inventory,
    compass,
    toggle_weapon,
};

inline constexpr std::size_t input_action_count =
    std::to_underlying(input_action::toggle_weapon) + 1;

inline constexpr std::array<std::string_view, input_action_count> input_action_names{
    "jump",      "sprint",    "attack",   "block",     "dodge",   "ability_1",
    "ability_2", "ability_3", "interact", "inventory", "compass", "toggle_weapon",
};

[[nodiscard]] constexpr auto input_action_name(input_action action) -> std::string_view {
    return input_action_names[std::to_underlying(action)];
}

[[nodiscard]] constexpr auto input_action_from_name(std::string_view name)
    -> std::optional<input_action> {
    for (std::size_t i = 0; i < input_action_count; ++i) {
        if (input_action_names[i] == name) {
            return static_cast<input_action>(i);
        }
    }
    return std::nullopt;
}

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

    input_action_mask held     = 0;
    input_action_mask pressed  = 0;
    input_action_mask released = 0;

    [[nodiscard]] constexpr auto is_held(input_action action) const -> bool {
        return (held & action_bit(action)) != 0;
    }

    [[nodiscard]] constexpr auto was_pressed(input_action action) const -> bool {
        return (pressed & action_bit(action)) != 0;
    }

    [[nodiscard]] constexpr auto was_released(input_action action) const -> bool {
        return (released & action_bit(action)) != 0;
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

inline constexpr std::size_t move_direction_count = std::to_underlying(move_direction::right) + 1;

inline constexpr std::array<std::string_view, move_direction_count> move_direction_names{
    "forward", "back", "left", "right",
};

[[nodiscard]] constexpr auto move_direction_name(move_direction direction) -> std::string_view {
    return move_direction_names[std::to_underlying(direction)];
}

[[nodiscard]] constexpr auto move_direction_from_name(std::string_view name)
    -> std::optional<move_direction> {
    for (std::size_t i = 0; i < move_direction_count; ++i) {
        if (move_direction_names[i] == name) {
            return static_cast<move_direction>(i);
        }
    }
    return std::nullopt;
}

struct key_action_binding {
    keyboard::keys key;
    input_action action;

    [[nodiscard]] auto operator==(const key_action_binding&) const -> bool = default;
};

struct button_action_binding {
    mouse::buttons button;
    input_action action;

    [[nodiscard]] auto operator==(const button_action_binding&) const -> bool = default;
};

struct key_move_binding {
    keyboard::keys key;
    move_direction direction;

    [[nodiscard]] auto operator==(const key_move_binding&) const -> bool = default;
};

struct input_bindings {
    std::vector<key_action_binding> key_actions;
    std::vector<button_action_binding> button_actions;
    std::vector<key_move_binding> key_moves;

    [[nodiscard]] auto operator==(const input_bindings&) const -> bool = default;
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

    [[nodiscard]] auto bindings() const -> const input_bindings&;
    auto set_bindings(input_bindings bindings) -> void;

    [[nodiscard]] auto settings() -> input_settings&;

private:
    [[nodiscard]] auto held_actions_() const -> input_action_mask;
    auto note_edges_(input_action_mask before) -> void;

    input_bindings bindings_;
    input_settings settings_;

    std::array<bool, keyboard::key_count> keys_down_{};
    std::array<bool, mouse::button_count> buttons_down_{};

    input_action_mask pressed_  = 0;
    input_action_mask released_ = 0;

    float32 look_yaw_degrees_;
    float32 look_pitch_degrees_;
    float32 zoom_delta_ = 0.0f;

    float64 cursor_x_  = 0.0;
    float64 cursor_y_  = 0.0;
    bool cursor_known_ = false;
};

}  // namespace vw::game
