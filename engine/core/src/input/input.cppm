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

}  // namespace mouse

}  // namespace vw
