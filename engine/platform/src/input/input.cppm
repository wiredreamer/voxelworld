export module vw.platform:input;

import vw.core;

export namespace vw::plat {

namespace keyboard = ::vw::keyboard;
namespace mouse    = ::vw::mouse;

enum class cursor_modes : uint8 {
    NORMAL,
    HIDDEN,
    DISABLED,
};

enum class input_modes : uint8 {
    STICKY_KEYS,
    STICKY_MOUSE_BUTTONS,
    LOCK_KEY_MODS,
    RAW_MOUSE_MOTION,
};

}  // namespace vw::plat
