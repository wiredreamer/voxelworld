module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

free_camera_controller::free_camera_controller(
    float mouse_sensitivity, float camera_speed
)
    : mouse_sensitivity_(mouse_sensitivity)
    , camera_speed_(camera_speed)
    , key_press_sub_(0)
    , mouse_move_sub_(0) {}

auto free_camera_controller::setup(
    window& window, camera& camera
) -> void {
    window_ = &window;
    camera_ = &camera;

    const auto cursor_pos = window_->get_cursor_pos();
    last_mouse_x_         = cursor_pos.x;
    last_mouse_y_         = cursor_pos.y;

    mouse_initialized_ = true;

    mouse_move_sub_ = window_->sub<mouse_move_event>([this](const mouse_move_event& event) -> bool {
        if (mouse_captured_) {
            handle_mouse_moved_(event.x, event.y);
        }
        return false;
    });
}

auto free_camera_controller::update(
    float delta_time
) const -> void {
    if (!camera_ || !window_)
        return;

    update_camera_movement_(delta_time);
}

auto free_camera_controller::set_mouse_sensitivity(
    float sensitivity
) -> void {
    mouse_sensitivity_ = sensitivity;
}

auto free_camera_controller::get_mouse_sensitivity() const -> float {
    return mouse_sensitivity_;
}

auto free_camera_controller::set_camera_speed(
    float speed
) -> void {
    camera_speed_ = speed;
}

auto free_camera_controller::get_camera_speed() const -> float {
    return camera_speed_;
}

auto free_camera_controller::is_mouse_captured() const -> bool {
    return mouse_captured_;
}

auto free_camera_controller::handle_mouse_moved_(
    double x, double y
) -> void {
    if (!camera_) {
        return;
    }

    if (!mouse_initialized_) {
        last_mouse_x_      = x;
        last_mouse_y_      = y;
        mouse_initialized_ = true;
        return;
    }

    const double delta_x = x - last_mouse_x_;
    const double delta_y = y - last_mouse_y_;

    last_mouse_x_ = x;
    last_mouse_y_ = y;

    if (delta_x != 0 || delta_y != 0) {
        const float yaw_delta   = static_cast<float>(delta_x) * mouse_sensitivity_;
        const float pitch_delta = -static_cast<float>(delta_y) * mouse_sensitivity_;

        if (camera_->is_orthographic()) {
            camera_->turn_about_view_centre(pitch_delta, yaw_delta);
        } else {
            camera_->rotate(pitch_delta, yaw_delta);
        }
    }
}

auto free_camera_controller::update_camera_movement_(
    float delta_time
) const -> void {
    if (!keyboard_control_enabled_) {
        return;
    }

    const float move_speed = camera_speed_ * delta_time;

    if (window_->is_key_pressed(keyboard::keys::W)) {
        camera_->move_closer(move_speed);
    }
    if (window_->is_key_pressed(keyboard::keys::S)) {
        camera_->move_closer(-move_speed);
    }

    if (window_->is_key_pressed(keyboard::keys::A)) {
        camera_->move_right(-move_speed);
    }
    if (window_->is_key_pressed(keyboard::keys::D)) {
        camera_->move_right(move_speed);
    }

    const vec3f lift =
        camera_->is_orthographic() ? camera_->get_up() : vec3f{0.0f, 1.0f, 0.0f};

    if (window_->is_key_pressed(keyboard::keys::SPACE)) {
        camera_->set_position(camera_->get_position() + lift * move_speed);
    }
    if (window_->is_key_pressed(keyboard::keys::LEFT_SHIFT)) {
        camera_->set_position(camera_->get_position() - lift * move_speed);
    }
}

auto free_camera_controller::set_mouse_captured(
    bool captured
) -> void {
    mouse_captured_ = captured;

    window_->set_cursor_mode(mouse_captured_ ? cursor_modes::DISABLED : cursor_modes::NORMAL);
    window_->set_input_mode(input_modes::RAW_MOUSE_MOTION, mouse_captured_);

    mouse_initialized_ = false;
}

auto free_camera_controller::toggle_mouse_captured() -> void {
    set_mouse_captured(!mouse_captured_);
}

auto free_camera_controller::keyboard_control_enabled() const -> bool {
    return keyboard_control_enabled_;
}

auto free_camera_controller::set_keyboard_control_enabled(
    bool enabled
) -> void {
    keyboard_control_enabled_ = enabled;
}

auto free_camera_controller::toggle_keyboard_control_enabled() -> void {
    keyboard_control_enabled_ = !keyboard_control_enabled_;
}

}  // namespace vw::gfx
