export module vw.gfx:camera.player_input_controller;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :camera;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {


struct player_input_params {
    float32 mouse_sensitivity = 0.1f;
};

class player_input_controller {
public:
    explicit player_input_controller(window& window, player_input_params params = {});
    ~player_input_controller();

    player_input_controller(const player_input_controller&)            = delete;
    auto operator=(const player_input_controller&) -> player_input_controller& = delete;
    player_input_controller(player_input_controller&&)                 = delete;
    auto operator=(player_input_controller&&) -> player_input_controller& = delete;

    [[nodiscard]] auto get_input_state() -> player_input_state;

    [[nodiscard]] auto get_params() -> player_input_params&;

    auto set_mouse_captured(bool captured) -> void;
    [[nodiscard]] auto is_mouse_captured() const -> bool;

private:
    window* window_;
    player_input_params params_;

    bool mouse_captured_{false};

    double last_mouse_x_{0.0};
    double last_mouse_y_{0.0};
    bool mouse_initialized_{false};

    float32 accumulated_look_x_{0.0f};
    float32 accumulated_look_y_{0.0f};
    float32 accumulated_scroll_{0.0f};

    bool attack_pressed_{false};

    event_sub<mouse_move_event> mouse_move_sub_;
    event_sub<mouse_scroll_event> mouse_scroll_sub_;
    event_sub<mouse_press_event> mouse_press_sub_;
};

}  // namespace vw::gfx
