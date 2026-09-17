export module vw.platform:input;

import vw.core;

export namespace vw::plat {

namespace keyboard {

enum class keys : int32 {
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,

    KEY_0 = 48,
    KEY_1 = 49,
    KEY_2 = 50,
    KEY_3 = 51,
    KEY_4 = 52,
    KEY_5 = 53,
    KEY_6 = 54,
    KEY_7 = 55,
    KEY_8 = 56,
    KEY_9 = 57,

    F1  = 290,
    F2  = 291,
    F3  = 292,
    F4  = 293,
    F5  = 294,
    F6  = 295,
    F7  = 296,
    F8  = 297,
    F9  = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,

    ESCAPE    = 256,
    ENTER     = 257,
    TAB       = 258,
    SPACE     = 32,
    BACKSPACE = 259,
    DELETE    = 261,
    INSERT    = 260,
    HOME      = 268,
    END       = 269,
    PAGE_UP   = 266,
    PAGE_DOWN = 267,

    LEFT  = 263,
    RIGHT = 262,
    UP    = 265,
    DOWN  = 264,

    LEFT_SHIFT    = 340,
    RIGHT_SHIFT   = 344,
    LEFT_CONTROL  = 341,
    RIGHT_CONTROL = 345,
    LEFT_ALT      = 342,
    RIGHT_ALT     = 346,
    LEFT_SUPER    = 343,
    RIGHT_SUPER   = 347,

    GRAVE_ACCENT  = 96,
    MINUS         = 45,
    EQUAL         = 61,
    LEFT_BRACKET  = 91,
    RIGHT_BRACKET = 93,
    BACKSLASH     = 92,
    SEMICOLON     = 59,
    APOSTROPHE    = 39,
    COMMA         = 44,
    PERIOD        = 46,
    SLASH         = 47,

    NUM_0        = 320,
    NUM_1        = 321,
    NUM_2        = 322,
    NUM_3        = 323,
    NUM_4        = 324,
    NUM_5        = 325,
    NUM_6        = 326,
    NUM_7        = 327,
    NUM_8        = 328,
    NUM_9        = 329,
    NUM_DECIMAL  = 330,
    NUM_DIVIDE   = 331,
    NUM_MULTIPLY = 332,
    NUM_SUBTRACT = 333,
    NUM_ADD      = 334,
    NUM_ENTER    = 335,
    NUM_EQUAL    = 336
};

enum class mods : int32 {
    SHIFT     = 0x0001,
    CTRL      = 0x0002,
    ALT       = 0x0004,
    SUPER     = 0x0008,
    CAPS_LOCK = 0x0010,
    NUM_LOCK  = 0x0020
};

[[nodiscard]] auto operator&(mods lhs, mods rhs) -> bool {
    return (static_cast<int32>(lhs) & static_cast<int32>(rhs)) != 0;
}

}  // namespace keyboard

namespace mouse {

enum class buttons : int32 {
    LEFT     = 0,
    RIGHT    = 1,
    MIDDLE   = 2,
    BUTTON_4 = 3,
    BUTTON_5 = 4,
    BUTTON_6 = 5,
    BUTTON_7 = 6,
    BUTTON_8 = 7
};

}  // namespace mouse

enum class cursor_modes : int32 {
    NORMAL   = 0x00034001,
    HIDDEN   = 0x00034002,
    DISABLED = 0x00034003
};

enum class input_modes : int32 {
    CURSOR               = 0x00033001,
    STICKY_KEYS          = 0x00033002,
    STICKY_MOUSE_BUTTONS = 0x00033003,
    LOCK_KEY_MODS        = 0x00033004,
    RAW_MOUSE_MOTION     = 0x00033005
};

}  // namespace vw::plat
