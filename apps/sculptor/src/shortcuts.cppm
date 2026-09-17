export module vw.sculptor:shortcuts;

import std;

import vw.core;
import vw.platform;
import :state;

export namespace vw::sculptor {

enum class command : uint8 {
    undo,
    redo,

    file_new,
    file_open,
    file_save,
    file_save_as,

    tool_select,
    tool_add_voxel,
    tool_remove_voxel,
    tool_paint,
    tool_color_picker,
    tool_move_pivot,

    gizmo_move,
    gizmo_rotate,
    gizmo_scale,

    toggle_sockets,
    enter_animation,
    toggle_timeline,

    play_pause,
    step_back,
    step_forward,

    record_key,
    record_key_all,
    prev_key,
    next_key,
};

inline constexpr uint32 mod_none  = 0;
inline constexpr uint32 mod_shift = static_cast<uint32>(plat::keyboard::mods::SHIFT);
inline constexpr uint32 mod_ctrl  = static_cast<uint32>(plat::keyboard::mods::CTRL);
inline constexpr uint32 mod_alt   = static_cast<uint32>(plat::keyboard::mods::ALT);
inline constexpr uint32 mod_super = static_cast<uint32>(plat::keyboard::mods::SUPER);

struct shortcut {
    plat::keyboard::keys key;
    uint32 mods = mod_none;
    command cmd = command::undo;

    std::string_view group;
    std::string_view keys;
    std::string_view title;
};

inline constexpr std::array shortcuts{
    shortcut{plat::keyboard::keys::Z, mod_ctrl, command::undo, "Edit", "Ctrl+Z", "Undo"},
    shortcut{
        plat::keyboard::keys::Z, mod_ctrl | mod_shift, command::redo, "Edit", "Ctrl+Shift+Z",
        "Redo"
    },

    shortcut{plat::keyboard::keys::N, mod_ctrl, command::file_new, "File", "Ctrl+N", "New file"},
    shortcut{plat::keyboard::keys::O, mod_ctrl, command::file_open, "File", "Ctrl+O", "Open file"},
    shortcut{plat::keyboard::keys::S, mod_ctrl, command::file_save, "File", "Ctrl+S", "Save"},
    shortcut{
        plat::keyboard::keys::S, mod_ctrl | mod_shift, command::file_save_as, "File",
        "Ctrl+Shift+S", "Save as"
    },

    shortcut{plat::keyboard::keys::Q, mod_none, command::tool_select, "Tools", "Q", "Select entity"},
    shortcut{plat::keyboard::keys::B, mod_none, command::tool_add_voxel, "Tools", "B", "Add voxel"},
    shortcut{
        plat::keyboard::keys::X, mod_none, command::tool_remove_voxel, "Tools", "X", "Remove voxel"
    },
    shortcut{plat::keyboard::keys::C, mod_none, command::tool_paint, "Tools", "C", "Paint voxel"},
    shortcut{
        plat::keyboard::keys::V, mod_none, command::tool_color_picker, "Tools", "V", "Color picker"
    },
    shortcut{plat::keyboard::keys::G, mod_none, command::tool_move_pivot, "Tools", "G", "Move pivot"},

    shortcut{plat::keyboard::keys::E, mod_none, command::gizmo_move, "Gizmo", "E", "Move"},
    shortcut{plat::keyboard::keys::R, mod_none, command::gizmo_rotate, "Gizmo", "R", "Rotate"},
    shortcut{plat::keyboard::keys::T, mod_none, command::gizmo_scale, "Gizmo", "T", "Scale"},

    shortcut{plat::keyboard::keys::S, mod_alt, command::toggle_sockets, "View", "Alt+S", "Sockets"},
    shortcut{
        plat::keyboard::keys::A, mod_alt, command::enter_animation, "View", "Alt+A", "Animate"
    },
    shortcut{
        plat::keyboard::keys::T, mod_alt, command::toggle_timeline, "View", "Alt+T", "Timeline"
    },

    shortcut{
        plat::keyboard::keys::SPACE, mod_none, command::play_pause, "Animation", "Space",
        "Play / pause"
    },
    shortcut{
        plat::keyboard::keys::LEFT, mod_none, command::step_back, "Animation", "Left",
        "Step backward"
    },
    shortcut{
        plat::keyboard::keys::RIGHT, mod_none, command::step_forward, "Animation", "Right",
        "Step forward"
    },
    shortcut{
        plat::keyboard::keys::K, mod_none, command::record_key, "Animation", "K",
        "Key current channel"
    },
    shortcut{
        plat::keyboard::keys::K, mod_shift, command::record_key_all, "Animation", "Shift+K",
        "Key all channels"
    },
    shortcut{
        plat::keyboard::keys::LEFT, mod_alt, command::prev_key, "Animation", "Alt+Left",
        "Previous key"
    },
    shortcut{
        plat::keyboard::keys::RIGHT, mod_alt, command::next_key, "Animation", "Alt+Right",
        "Next key"
    },
};

[[nodiscard]] auto match(const plat::key_press_event& ev) -> std::optional<command>;

[[nodiscard]] auto is_available(command cmd, const app_state& state) -> bool;

[[nodiscard]] auto keys_of(command cmd) -> std::string_view;

[[nodiscard]] auto tool_of(command cmd) -> tools;
[[nodiscard]] auto command_for_tool(tools tool) -> std::optional<command>;

}  // namespace vw::sculptor
