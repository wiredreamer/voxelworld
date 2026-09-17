export module vw.platform:window;

import std;

import vw.core;
import :event;
import :input;

namespace vw::plat::detail {
struct window_callbacks;
}  // namespace vw::plat::detail

export namespace vw::plat {

class window final {
public:
    window(int32 width, int32 height, std::string_view title);
    ~window();

    window(const window&)                      = delete;
    auto operator=(const window&) -> window&   = delete;
    window(window&&)                           = delete;
    auto operator=(window&&) -> window&        = delete;

    [[nodiscard]] auto should_close() const -> bool;

    auto poll_events() -> void;

    [[nodiscard]] auto framebuffer_size() const -> vec2i;

    [[nodiscard]] auto create_surface(uint64 instance) const -> uint64;

    [[nodiscard]] static auto required_extensions() -> std::vector<const char*>;

    [[nodiscard]] auto get_size() const -> vec2i {
        return size_;
    }

    [[nodiscard]] auto get_width() const -> int32 {
        return size_.x;
    }

    [[nodiscard]] auto get_height() const -> int32 {
        return size_.y;
    }

    [[nodiscard]] auto is_key_pressed(keyboard::keys key) const -> bool;
    [[nodiscard]] auto is_mouse_button_pressed(mouse::buttons button) const -> bool;

    [[nodiscard]] auto get_cursor_pos() const -> vec2d;

    auto set_cursor_pos(vec2d pos) const -> void;
    auto set_cursor_pos(float64 x, float64 y) const -> void;

    auto set_cursor_mode(cursor_modes mode) const -> void;
    auto set_input_mode(input_modes mode, bool value) const -> void;

    auto set_title(std::string_view title) const -> void;

    auto set_size(vec2i size) const -> void;
    auto set_size(int32 width, int32 height) const -> void;

    auto set_position(vec2i pos) const -> void;
    auto set_position(int32 x, int32 y) const -> void;

    auto maximize() const -> void;
    auto minimize() const -> void;
    auto restore() const -> void;

    [[nodiscard]] auto native_handle() const -> void* {
        return handle_;
    }

    template <event_type E, event_callback_type<E> F>
    auto sub(F&& callback) -> event_sub<E> {
        return event_dispatcher_.sub<E>(std::forward<F>(callback));
    }

    template <event_type E>
    auto unsub(event_sub<E> sub) -> void {
        return event_dispatcher_.unsub(sub);
    }

private:
    friend struct detail::window_callbacks;

    auto on_key_(int32 key, int32 scancode, int32 mods, bool pressed, bool repeat) -> void;
    auto on_mouse_button_(int32 button, int32 mods, bool pressed) -> void;
    auto on_mouse_move_(float64 x, float64 y) -> void;
    auto on_mouse_scroll_(float64 offset_x, float64 offset_y) -> void;
    auto on_resize_(int32 width, int32 height) -> void;
    auto on_focus_(bool focused) -> void;
    auto on_close_() -> void;

    void* handle_ = nullptr;

    mutable vec2i size_;
    mutable vec2d last_cursor_pos_{0.0, 0.0};

    event_dispatcher event_dispatcher_;
};

}  // namespace vw::plat
