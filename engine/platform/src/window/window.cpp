module;

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

module vw.platform;

import std;
import vw.core;

namespace vw::plat {
namespace {

constexpr log::log_category lc{"window"};

auto as_glfw(void* handle) -> GLFWwindow* {
    return static_cast<GLFWwindow*>(handle);
}

// см. docs/ENGINE.md#коды-клавиш
struct key_code {
    keyboard::keys key;
    int glfw;
};

using keys = keyboard::keys;

constexpr std::array key_codes{
    key_code{keys::A, GLFW_KEY_A},
    key_code{keys::B, GLFW_KEY_B},
    key_code{keys::C, GLFW_KEY_C},
    key_code{keys::D, GLFW_KEY_D},
    key_code{keys::E, GLFW_KEY_E},
    key_code{keys::F, GLFW_KEY_F},
    key_code{keys::G, GLFW_KEY_G},
    key_code{keys::H, GLFW_KEY_H},
    key_code{keys::I, GLFW_KEY_I},
    key_code{keys::J, GLFW_KEY_J},
    key_code{keys::K, GLFW_KEY_K},
    key_code{keys::L, GLFW_KEY_L},
    key_code{keys::M, GLFW_KEY_M},
    key_code{keys::N, GLFW_KEY_N},
    key_code{keys::O, GLFW_KEY_O},
    key_code{keys::P, GLFW_KEY_P},
    key_code{keys::Q, GLFW_KEY_Q},
    key_code{keys::R, GLFW_KEY_R},
    key_code{keys::S, GLFW_KEY_S},
    key_code{keys::T, GLFW_KEY_T},
    key_code{keys::U, GLFW_KEY_U},
    key_code{keys::V, GLFW_KEY_V},
    key_code{keys::W, GLFW_KEY_W},
    key_code{keys::X, GLFW_KEY_X},
    key_code{keys::Y, GLFW_KEY_Y},
    key_code{keys::Z, GLFW_KEY_Z},
    key_code{keys::KEY_0, GLFW_KEY_0},
    key_code{keys::KEY_1, GLFW_KEY_1},
    key_code{keys::KEY_2, GLFW_KEY_2},
    key_code{keys::KEY_3, GLFW_KEY_3},
    key_code{keys::KEY_4, GLFW_KEY_4},
    key_code{keys::KEY_5, GLFW_KEY_5},
    key_code{keys::KEY_6, GLFW_KEY_6},
    key_code{keys::KEY_7, GLFW_KEY_7},
    key_code{keys::KEY_8, GLFW_KEY_8},
    key_code{keys::KEY_9, GLFW_KEY_9},
    key_code{keys::F1, GLFW_KEY_F1},
    key_code{keys::F2, GLFW_KEY_F2},
    key_code{keys::F3, GLFW_KEY_F3},
    key_code{keys::F4, GLFW_KEY_F4},
    key_code{keys::F5, GLFW_KEY_F5},
    key_code{keys::F6, GLFW_KEY_F6},
    key_code{keys::F7, GLFW_KEY_F7},
    key_code{keys::F8, GLFW_KEY_F8},
    key_code{keys::F9, GLFW_KEY_F9},
    key_code{keys::F10, GLFW_KEY_F10},
    key_code{keys::F11, GLFW_KEY_F11},
    key_code{keys::F12, GLFW_KEY_F12},
    key_code{keys::ESCAPE, GLFW_KEY_ESCAPE},
    key_code{keys::ENTER, GLFW_KEY_ENTER},
    key_code{keys::TAB, GLFW_KEY_TAB},
    key_code{keys::SPACE, GLFW_KEY_SPACE},
    key_code{keys::BACKSPACE, GLFW_KEY_BACKSPACE},
    key_code{keys::DELETE, GLFW_KEY_DELETE},
    key_code{keys::INSERT, GLFW_KEY_INSERT},
    key_code{keys::HOME, GLFW_KEY_HOME},
    key_code{keys::END, GLFW_KEY_END},
    key_code{keys::PAGE_UP, GLFW_KEY_PAGE_UP},
    key_code{keys::PAGE_DOWN, GLFW_KEY_PAGE_DOWN},
    key_code{keys::LEFT, GLFW_KEY_LEFT},
    key_code{keys::RIGHT, GLFW_KEY_RIGHT},
    key_code{keys::UP, GLFW_KEY_UP},
    key_code{keys::DOWN, GLFW_KEY_DOWN},
    key_code{keys::LEFT_SHIFT, GLFW_KEY_LEFT_SHIFT},
    key_code{keys::RIGHT_SHIFT, GLFW_KEY_RIGHT_SHIFT},
    key_code{keys::LEFT_CONTROL, GLFW_KEY_LEFT_CONTROL},
    key_code{keys::RIGHT_CONTROL, GLFW_KEY_RIGHT_CONTROL},
    key_code{keys::LEFT_ALT, GLFW_KEY_LEFT_ALT},
    key_code{keys::RIGHT_ALT, GLFW_KEY_RIGHT_ALT},
    key_code{keys::LEFT_SUPER, GLFW_KEY_LEFT_SUPER},
    key_code{keys::RIGHT_SUPER, GLFW_KEY_RIGHT_SUPER},
    key_code{keys::GRAVE_ACCENT, GLFW_KEY_GRAVE_ACCENT},
    key_code{keys::MINUS, GLFW_KEY_MINUS},
    key_code{keys::EQUAL, GLFW_KEY_EQUAL},
    key_code{keys::LEFT_BRACKET, GLFW_KEY_LEFT_BRACKET},
    key_code{keys::RIGHT_BRACKET, GLFW_KEY_RIGHT_BRACKET},
    key_code{keys::BACKSLASH, GLFW_KEY_BACKSLASH},
    key_code{keys::SEMICOLON, GLFW_KEY_SEMICOLON},
    key_code{keys::APOSTROPHE, GLFW_KEY_APOSTROPHE},
    key_code{keys::COMMA, GLFW_KEY_COMMA},
    key_code{keys::PERIOD, GLFW_KEY_PERIOD},
    key_code{keys::SLASH, GLFW_KEY_SLASH},
    key_code{keys::NUM_0, GLFW_KEY_KP_0},
    key_code{keys::NUM_1, GLFW_KEY_KP_1},
    key_code{keys::NUM_2, GLFW_KEY_KP_2},
    key_code{keys::NUM_3, GLFW_KEY_KP_3},
    key_code{keys::NUM_4, GLFW_KEY_KP_4},
    key_code{keys::NUM_5, GLFW_KEY_KP_5},
    key_code{keys::NUM_6, GLFW_KEY_KP_6},
    key_code{keys::NUM_7, GLFW_KEY_KP_7},
    key_code{keys::NUM_8, GLFW_KEY_KP_8},
    key_code{keys::NUM_9, GLFW_KEY_KP_9},
    key_code{keys::NUM_DECIMAL, GLFW_KEY_KP_DECIMAL},
    key_code{keys::NUM_DIVIDE, GLFW_KEY_KP_DIVIDE},
    key_code{keys::NUM_MULTIPLY, GLFW_KEY_KP_MULTIPLY},
    key_code{keys::NUM_SUBTRACT, GLFW_KEY_KP_SUBTRACT},
    key_code{keys::NUM_ADD, GLFW_KEY_KP_ADD},
    key_code{keys::NUM_ENTER, GLFW_KEY_KP_ENTER},
    key_code{keys::NUM_EQUAL, GLFW_KEY_KP_EQUAL},
};

static_assert(key_codes.size() + 1 == keyboard::key_count);

constexpr auto glfw_of_key = [] {
    std::array<int, keyboard::key_count> out{};
    out.fill(GLFW_KEY_UNKNOWN);
    for (const auto& code : key_codes) {
        out[std::to_underlying(code.key)] = code.glfw;
    }
    return out;
}();

constexpr auto key_of_glfw = [] {
    std::array<keys, GLFW_KEY_LAST + 1> out{};
    out.fill(keys::UNKNOWN);
    for (const auto& code : key_codes) {
        out[static_cast<std::size_t>(code.glfw)] = code.key;
    }
    return out;
}();

auto to_key(int glfw_key) -> keys {
    return glfw_key >= 0 && glfw_key <= GLFW_KEY_LAST
        ? key_of_glfw[static_cast<std::size_t>(glfw_key)]
        : keys::UNKNOWN;
}

auto to_glfw(keys key) -> int {
    return glfw_of_key[std::to_underlying(key)];
}

struct mod_code {
    keyboard::mods mod;
    int glfw;
};

constexpr std::array mod_codes{
    mod_code{keyboard::mods::SHIFT, GLFW_MOD_SHIFT},
    mod_code{keyboard::mods::CTRL, GLFW_MOD_CONTROL},
    mod_code{keyboard::mods::ALT, GLFW_MOD_ALT},
    mod_code{keyboard::mods::SUPER, GLFW_MOD_SUPER},
    mod_code{keyboard::mods::CAPS_LOCK, GLFW_MOD_CAPS_LOCK},
    mod_code{keyboard::mods::NUM_LOCK, GLFW_MOD_NUM_LOCK},
};

auto to_mods(int glfw_mods) -> keyboard::mods {
    auto out = keyboard::mods::NONE;
    for (const auto& code : mod_codes) {
        if ((glfw_mods & code.glfw) != 0) {
            out = out | code.mod;
        }
    }
    return out;
}

constexpr std::array glfw_of_button{
    GLFW_MOUSE_BUTTON_1, GLFW_MOUSE_BUTTON_2, GLFW_MOUSE_BUTTON_3, GLFW_MOUSE_BUTTON_4,
    GLFW_MOUSE_BUTTON_5, GLFW_MOUSE_BUTTON_6, GLFW_MOUSE_BUTTON_7, GLFW_MOUSE_BUTTON_8,
};

static_assert(glfw_of_button.size() == mouse::button_count);

auto to_button(int glfw_button) -> std::optional<mouse::buttons> {
    const auto it = std::ranges::find(glfw_of_button, glfw_button);
    if (it == glfw_of_button.end()) {
        return std::nullopt;
    }
    return static_cast<mouse::buttons>(it - glfw_of_button.begin());
}

auto to_glfw(mouse::buttons button) -> int {
    return glfw_of_button[std::to_underlying(button)];
}

auto to_glfw(cursor_modes mode) -> int {
    switch (mode) {
        case cursor_modes::NORMAL:
            return GLFW_CURSOR_NORMAL;
        case cursor_modes::HIDDEN:
            return GLFW_CURSOR_HIDDEN;
        case cursor_modes::DISABLED:
            return GLFW_CURSOR_DISABLED;
    }
    return GLFW_CURSOR_NORMAL;
}

auto to_glfw(input_modes mode) -> int {
    switch (mode) {
        case input_modes::STICKY_KEYS:
            return GLFW_STICKY_KEYS;
        case input_modes::STICKY_MOUSE_BUTTONS:
            return GLFW_STICKY_MOUSE_BUTTONS;
        case input_modes::LOCK_KEY_MODS:
            return GLFW_LOCK_KEY_MODS;
        case input_modes::RAW_MOUSE_MOTION:
            return GLFW_RAW_MOUSE_MOTION;
    }
    return GLFW_STICKY_KEYS;
}

}  // namespace

namespace detail {

struct window_callbacks {
    [[nodiscard]] static auto owner_of(GLFWwindow* handle) -> window* {
        return static_cast<window*>(glfwGetWindowUserPointer(handle));
    }

    static auto key(GLFWwindow* handle, int key, int scancode, int action, int mods) -> void {
        owner_of(handle)->on_key_(
            key, scancode, mods, action != GLFW_RELEASE, action == GLFW_REPEAT);
    }

    static auto mouse_button(GLFWwindow* handle, int button, int action, int mods) -> void {
        if (action == GLFW_PRESS || action == GLFW_RELEASE) {
            owner_of(handle)->on_mouse_button_(button, mods, action == GLFW_PRESS);
        }
    }

    static auto mouse_motion(GLFWwindow* handle, double pos_x, double pos_y) -> void {
        owner_of(handle)->on_mouse_move_(pos_x, pos_y);
    }

    static auto mouse_scroll(GLFWwindow* handle, double offset_x, double offset_y) -> void {
        owner_of(handle)->on_mouse_scroll_(offset_x, offset_y);
    }

    static auto resize(GLFWwindow* handle, int width, int height) -> void {
        owner_of(handle)->on_resize_(width, height);
    }

    static auto focus(GLFWwindow* handle, int focused) -> void {
        owner_of(handle)->on_focus_(focused == GLFW_TRUE);
    }

    static auto close(GLFWwindow* handle) -> void {
        owner_of(handle)->on_close_();
    }
};

}  // namespace detail

window::window(int32 width, int32 height, std::string_view title) : size_(width, height) {
    if (glfwInit() == GLFW_FALSE) {
        throw std::runtime_error("failed to initialize glfw");
    }

    log::info(lc, "GLFW {}", glfwGetVersionString());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    handle_ = glfwCreateWindow(width, height, std::string(title).c_str(), nullptr, nullptr);
    if (handle_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("failed to create glfw window");
    }

    auto* handle = as_glfw(handle_);
    glfwSetWindowUserPointer(handle, this);

    glfwSetKeyCallback(handle, detail::window_callbacks::key);
    glfwSetMouseButtonCallback(handle, detail::window_callbacks::mouse_button);
    glfwSetCursorPosCallback(handle, detail::window_callbacks::mouse_motion);
    glfwSetScrollCallback(handle, detail::window_callbacks::mouse_scroll);
    glfwSetFramebufferSizeCallback(handle, detail::window_callbacks::resize);
    glfwSetWindowFocusCallback(handle, detail::window_callbacks::focus);
    glfwSetWindowCloseCallback(handle, detail::window_callbacks::close);
}

window::~window() {
    if (handle_ != nullptr) {
        glfwDestroyWindow(as_glfw(handle_));
    }
    glfwTerminate();
}

auto window::on_key_(int32 key, int32 scancode, int32 mods, bool pressed, bool repeat) -> void {
    const auto typed_key  = to_key(key);
    const auto typed_mods = to_mods(mods);

    if (repeat) {
        key_repeat_event event(typed_key, scancode, typed_mods);
        event_dispatcher_.dispatch(event);
    } else if (pressed) {
        key_press_event event(typed_key, scancode, typed_mods);
        event_dispatcher_.dispatch(event);
    } else {
        key_release_event event(typed_key, scancode, typed_mods);
        event_dispatcher_.dispatch(event);
    }
}

auto window::on_mouse_button_(int32 button, int32 mods, bool pressed) -> void {
    const auto typed_button = to_button(button);
    if (!typed_button) {
        return;
    }
    const auto typed_mods = to_mods(mods);

    if (pressed) {
        mouse_press_event event(*typed_button, typed_mods);
        event_dispatcher_.dispatch(event);
    } else {
        mouse_release_event event(*typed_button, typed_mods);
        event_dispatcher_.dispatch(event);
    }
}

auto window::on_mouse_move_(float64 x, float64 y) -> void {
    last_cursor_pos_ = {x, y};

    mouse_move_event event(x, y);
    event_dispatcher_.dispatch(event);
}

auto window::on_mouse_scroll_(float64 offset_x, float64 offset_y) -> void {
    mouse_scroll_event event(offset_x, offset_y);
    event_dispatcher_.dispatch(event);
}

auto window::on_resize_(int32 width, int32 height) -> void {
    size_ = {width, height};

    window_resize_event event{width, height};
    event_dispatcher_.dispatch(event);
}

auto window::on_focus_(bool focused) -> void {
    window_focus_event event{focused};
    event_dispatcher_.dispatch(event);
}

auto window::on_close_() -> void {
    window_close_event event{};
    event_dispatcher_.dispatch(event);
}

auto window::should_close() const -> bool {
    return glfwWindowShouldClose(as_glfw(handle_)) == GLFW_TRUE;
}

auto window::poll_events() -> void {
    glfwPollEvents();
}

auto window::framebuffer_size() const -> vec2i {
    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(as_glfw(handle_), &width, &height);
    return {width, height};
}

auto window::create_surface(uint64 instance) const -> uint64 {
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    const VkResult result = glfwCreateWindowSurface(
        reinterpret_cast<VkInstance>(instance), as_glfw(handle_), nullptr, &surface);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create vulkan surface");
    }

    return reinterpret_cast<uint64>(surface);
}

auto window::required_extensions() -> std::vector<const char*> {
    uint32 count               = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&count);

    return {glfw_extensions, glfw_extensions + count};
}

auto window::is_key_pressed(keyboard::keys key) const -> bool {
    const int glfw_key = to_glfw(key);
    return glfw_key != GLFW_KEY_UNKNOWN && glfwGetKey(as_glfw(handle_), glfw_key) == GLFW_PRESS;
}

auto window::is_mouse_button_pressed(mouse::buttons button) const -> bool {
    return glfwGetMouseButton(as_glfw(handle_), to_glfw(button)) == GLFW_PRESS;
}

auto window::get_cursor_pos() const -> vec2d {
    float64 x = 0.0;
    float64 y = 0.0;
    glfwGetCursorPos(as_glfw(handle_), &x, &y);
    return {x, y};
}

auto window::set_cursor_pos(vec2d pos) const -> void {
    glfwSetCursorPos(as_glfw(handle_), pos.x, pos.y);
    last_cursor_pos_ = pos;
}

auto window::set_cursor_pos(float64 x, float64 y) const -> void {
    set_cursor_pos({x, y});
}

auto window::set_cursor_mode(cursor_modes mode) const -> void {
    glfwSetInputMode(as_glfw(handle_), GLFW_CURSOR, to_glfw(mode));
}

auto window::set_input_mode(input_modes mode, bool value) const -> void {
    glfwSetInputMode(as_glfw(handle_), to_glfw(mode), value ? GLFW_TRUE : GLFW_FALSE);
}

auto window::set_title(std::string_view title) const -> void {
    glfwSetWindowTitle(as_glfw(handle_), std::string(title).c_str());
}

auto window::set_size(vec2i size) const -> void {
    glfwSetWindowSize(as_glfw(handle_), size.x, size.y);
    size_ = size;
}

auto window::set_size(int32 width, int32 height) const -> void {
    set_size({width, height});
}

auto window::set_position(vec2i pos) const -> void {
    glfwSetWindowPos(as_glfw(handle_), pos.x, pos.y);
}

auto window::set_position(int32 x, int32 y) const -> void {
    set_position({x, y});
}

auto window::maximize() const -> void {
    glfwMaximizeWindow(as_glfw(handle_));
}

auto window::minimize() const -> void {
    glfwIconifyWindow(as_glfw(handle_));
}

auto window::restore() const -> void {
    glfwRestoreWindow(as_glfw(handle_));
}

}  // namespace vw::plat
