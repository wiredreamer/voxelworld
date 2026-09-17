export module vw.platform:event;

import std;

import vw.core;
import :input;

export namespace vw::plat {

struct event {
    virtual ~event() = default;
    bool handled     = false;
};

struct key_press_event final : event {
    keyboard::keys key;
    int32 scancode;
    keyboard::mods mods;

    key_press_event(keyboard::keys key, int32 scancode, keyboard::mods mods)
        : key(key), scancode(scancode), mods(mods) {}

    [[nodiscard]] auto with(keyboard::mods mod) const -> bool {
        return (static_cast<int32>(mods) & static_cast<int32>(mod)) == static_cast<int32>(mod);
    }
};

struct key_release_event final : event {
    keyboard::keys key;
    int32 scancode;
    keyboard::mods mods;

    key_release_event(keyboard::keys key, int32 scancode, keyboard::mods mods)
        : key(key), scancode(scancode), mods(mods) {}
};

struct key_repeat_event final : event {
    keyboard::keys key;
    int32 scancode;
    keyboard::mods mods;

    key_repeat_event(keyboard::keys key, int32 scancode, keyboard::mods mods)
        : key(key), scancode(scancode), mods(mods) {}
};

struct mouse_move_event final : event {
    float64 x;
    float64 y;

    mouse_move_event(float64 x, float64 y) : x(x), y(y) {}
};

struct mouse_press_event final : event {
    mouse::buttons button;
    keyboard::mods mods;

    mouse_press_event(mouse::buttons button, keyboard::mods mods) : button(button), mods(mods) {}
};

struct mouse_release_event final : event {
    mouse::buttons button;
    keyboard::mods mods;

    mouse_release_event(mouse::buttons button, keyboard::mods mods) : button(button), mods(mods) {}
};

struct mouse_scroll_event final : event {
    float64 offset_x;
    float64 offset_y;

    mouse_scroll_event(float64 offset_x, float64 offset_y)
        : offset_x(offset_x), offset_y(offset_y) {}
};

struct window_resize_event final : event {
    int32 width;
    int32 height;

    window_resize_event(int32 width, int32 height) : width(width), height(height) {}
};

struct window_focus_event final : event {
    bool focused;

    explicit window_focus_event(bool focused) : focused(focused) {}
};

struct window_close_event final : event {};

template <typename T>
concept event_type = std::derived_from<T, event>;

template <typename F, typename E>
concept event_callback_type =
    event_type<E> && std::invocable<F, E&> && std::same_as<std::invoke_result_t<F, E&>, bool>;

template <event_type>
struct event_sub {
    uint32 value = 0;
};

}  // namespace vw::plat

namespace vw::plat::detail {

auto next_event_id() -> uint32;

struct event_sink_base {
    event_sink_base()                                              = default;
    virtual ~event_sink_base()                                     = default;
    event_sink_base(const event_sink_base&)                        = delete;
    auto operator=(const event_sink_base&) -> event_sink_base&     = delete;
    event_sink_base(event_sink_base&&)                             = delete;
    auto operator=(event_sink_base&&) -> event_sink_base&          = delete;
};

}  // namespace vw::plat::detail

export namespace vw::plat {

template <event_type E>
auto event_id_of() -> uint32 {
    static const uint32 id = detail::next_event_id();
    return id;
}

class event_dispatcher {
public:
    template <event_type E, event_callback_type<E> F>
    auto sub(F&& callback) -> event_sub<E> {
        const event_sub<E> id{.value = next_id_++};
        sink_<E>().entries.emplace_back(id.value, std::forward<F>(callback));
        return id;
    }

    template <event_type E>
    auto unsub(event_sub<E> id) -> void {
        auto* sink = find_sink_<E>();
        if (sink == nullptr) {
            return;
        }
        std::erase_if(sink->entries, [id](const auto& entry) { return entry.id == id.value; });
    }

    template <event_type E>
    auto dispatch(E& event) -> void {
        auto* sink = find_sink_<E>();
        if (sink == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < sink->entries.size(); ++i) {
            if (sink->entries[i].callback(event)) {
                event.handled = true;
            }
        }
    }

private:
    template <event_type E>
    struct sink final : detail::event_sink_base {
        struct entry {
            uint32 id;
            std::function<bool(E&)> callback;
        };

        std::vector<entry> entries;
    };

    template <event_type E>
    auto sink_() -> sink<E>& {
        const uint32 id = event_id_of<E>();
        if (id >= sinks_.size()) {
            sinks_.resize(id + 1);
        }
        if (sinks_[id] == nullptr) {
            sinks_[id] = std::make_unique<sink<E>>();
        }
        return static_cast<sink<E>&>(*sinks_[id]);
    }

    template <event_type E>
    auto find_sink_() -> sink<E>* {
        const uint32 id = event_id_of<E>();
        return id < sinks_.size() && sinks_[id] != nullptr
            ? static_cast<sink<E>*>(sinks_[id].get())
            : nullptr;
    }

    std::vector<std::unique_ptr<detail::event_sink_base>> sinks_;
    uint32 next_id_ = 1;
};

}  // namespace vw::plat
