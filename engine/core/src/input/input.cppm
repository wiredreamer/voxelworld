export module vw.core:input;

import std;

import :types;

// см. docs/ENGINE.md#коды-клавиш
export namespace vw {

namespace keyboard {

enum class keys : uint8 {
    UNKNOWN,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    ESCAPE,
    ENTER,
    TAB,
    SPACE,
    BACKSPACE,
    DELETE,
    INSERT,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,

    LEFT,
    RIGHT,
    UP,
    DOWN,

    LEFT_SHIFT,
    RIGHT_SHIFT,
    LEFT_CONTROL,
    RIGHT_CONTROL,
    LEFT_ALT,
    RIGHT_ALT,
    LEFT_SUPER,
    RIGHT_SUPER,

    GRAVE_ACCENT,
    MINUS,
    EQUAL,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    BACKSLASH,
    SEMICOLON,
    APOSTROPHE,
    COMMA,
    PERIOD,
    SLASH,

    NUM_0,
    NUM_1,
    NUM_2,
    NUM_3,
    NUM_4,
    NUM_5,
    NUM_6,
    NUM_7,
    NUM_8,
    NUM_9,
    NUM_DECIMAL,
    NUM_DIVIDE,
    NUM_MULTIPLY,
    NUM_SUBTRACT,
    NUM_ADD,
    NUM_ENTER,
    NUM_EQUAL,
};

inline constexpr std::size_t key_count = std::to_underlying(keys::NUM_EQUAL) + 1;

inline constexpr std::array<std::string_view, key_count> key_names{
    "unknown",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
    "escape", "enter", "tab", "space", "backspace", "delete", "insert", "home", "end",
    "page_up", "page_down",
    "left", "right", "up", "down",
    "left_shift", "right_shift", "left_control", "right_control",
    "left_alt", "right_alt", "left_super", "right_super",
    "grave_accent", "minus", "equal", "left_bracket", "right_bracket", "backslash",
    "semicolon", "apostrophe", "comma", "period", "slash",
    "num_0", "num_1", "num_2", "num_3", "num_4", "num_5", "num_6", "num_7", "num_8", "num_9",
    "num_decimal", "num_divide", "num_multiply", "num_subtract", "num_add", "num_enter",
    "num_equal",
};

[[nodiscard]] constexpr auto key_name(keys key) -> std::string_view {
    return key_names[std::to_underlying(key)];
}

[[nodiscard]] constexpr auto key_from_name(std::string_view name) -> std::optional<keys> {
    for (std::size_t i = 1; i < key_count; ++i) {
        if (key_names[i] == name) {
            return static_cast<keys>(i);
        }
    }
    return std::nullopt;
}

enum class mods : uint8 {
    NONE      = 0,
    SHIFT     = 1 << 0,
    CTRL      = 1 << 1,
    ALT       = 1 << 2,
    SUPER     = 1 << 3,
    CAPS_LOCK = 1 << 4,
    NUM_LOCK  = 1 << 5,
};

[[nodiscard]] constexpr auto operator&(mods lhs, mods rhs) -> bool {
    return (std::to_underlying(lhs) & std::to_underlying(rhs)) != 0;
}

[[nodiscard]] constexpr auto operator|(mods lhs, mods rhs) -> mods {
    return static_cast<mods>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

}  // namespace keyboard

namespace mouse {

enum class buttons : uint8 {
    LEFT,
    RIGHT,
    MIDDLE,
    BUTTON_4,
    BUTTON_5,
    BUTTON_6,
    BUTTON_7,
    BUTTON_8,
};

inline constexpr std::size_t button_count = std::to_underlying(buttons::BUTTON_8) + 1;

inline constexpr std::array<std::string_view, button_count> button_names{
    "mouse_left", "mouse_right", "mouse_middle", "mouse_4",
    "mouse_5",    "mouse_6",     "mouse_7",      "mouse_8",
};

[[nodiscard]] constexpr auto button_name(buttons button) -> std::string_view {
    return button_names[std::to_underlying(button)];
}

[[nodiscard]] constexpr auto button_from_name(std::string_view name) -> std::optional<buttons> {
    for (std::size_t i = 0; i < button_count; ++i) {
        if (button_names[i] == name) {
            return static_cast<buttons>(i);
        }
    }
    return std::nullopt;
}

}  // namespace mouse

}  // namespace vw
