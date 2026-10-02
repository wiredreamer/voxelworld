module vw.game;

import std;
import vw.core;

namespace vw::game {

auto default_input_bindings() -> input_bindings {
    using keyboard::keys;

    return {
        .key_actions =
            {
                {keys::SPACE, input_action::jump},
                {keys::LEFT_SHIFT, input_action::sprint},
                {keys::KEY_1, input_action::toggle_weapon},
            },
        .button_actions =
            {
                {mouse::buttons::LEFT, input_action::attack},
            },
        .key_moves =
            {
                {keys::W, move_direction::forward},
                {keys::S, move_direction::back},
                {keys::A, move_direction::left},
                {keys::D, move_direction::right},
            },
    };
}

input_mapper::input_mapper(
    input_bindings bindings, input_settings settings
)
    : bindings_{std::move(bindings)}
    , settings_{settings}
    , look_yaw_degrees_{settings.initial_look_yaw_degrees}
    , look_pitch_degrees_{settings.initial_look_pitch_degrees} {}

auto input_mapper::key(
    keyboard::keys key, bool down
) -> void {
    auto& state = keys_down_[std::to_underlying(key)];
    if (down && !state) {
        for (const auto& binding : bindings_.key_actions) {
            if (binding.key == key) {
                pressed_ |= action_bit(binding.action);
            }
        }
    }
    state = down;
}

auto input_mapper::button(
    mouse::buttons button, bool down
) -> void {
    auto& state = buttons_down_[std::to_underlying(button)];
    if (down && !state) {
        for (const auto& binding : bindings_.button_actions) {
            if (binding.button == button) {
                pressed_ |= action_bit(binding.action);
            }
        }
    }
    state = down;
}

auto input_mapper::cursor_at(
    float64 x, float64 y
) -> void {
    if (cursor_known_) {
        const auto delta_x = static_cast<float32>(x - cursor_x_);
        const auto delta_y = static_cast<float32>(y - cursor_y_);

        look_yaw_degrees_ += delta_x * settings_.look_degrees_per_count;
        look_pitch_degrees_ = math::clamp(
            look_pitch_degrees_ - (delta_y * settings_.look_degrees_per_count),
            settings_.look_pitch_min_degrees, settings_.look_pitch_max_degrees
        );
    }

    cursor_x_     = x;
    cursor_y_     = y;
    cursor_known_ = true;
}

auto input_mapper::drop_cursor() -> void {
    cursor_known_ = false;
}

auto input_mapper::scroll(
    float32 delta
) -> void {
    zoom_delta_ += delta;
}

auto input_mapper::release_all() -> void {
    keys_down_.fill(false);
    buttons_down_.fill(false);
    pressed_ = 0;
}

auto input_mapper::take_frame() -> input_frame {
    input_frame frame{
        .look_yaw_degrees   = look_yaw_degrees_,
        .look_pitch_degrees = look_pitch_degrees_,
        .zoom_delta         = zoom_delta_,
        .pressed            = pressed_,
    };

    for (const auto& binding : bindings_.key_actions) {
        if (keys_down_[std::to_underlying(binding.key)]) {
            frame.held |= action_bit(binding.action);
        }
    }
    for (const auto& binding : bindings_.button_actions) {
        if (buttons_down_[std::to_underlying(binding.button)]) {
            frame.held |= action_bit(binding.action);
        }
    }

    for (const auto& binding : bindings_.key_moves) {
        if (!keys_down_[std::to_underlying(binding.key)]) {
            continue;
        }
        switch (binding.direction) {
            case move_direction::forward:
                frame.move_forward += 1.0f;
                break;
            case move_direction::back:
                frame.move_forward -= 1.0f;
                break;
            case move_direction::right:
                frame.move_right += 1.0f;
                break;
            case move_direction::left:
                frame.move_right -= 1.0f;
                break;
        }
    }

    const float32 length_squared =
        (frame.move_forward * frame.move_forward) + (frame.move_right * frame.move_right);
    if (length_squared > 0.0f) {
        const float32 inverse_length = 1.0f / std::sqrt(length_squared);
        frame.move_forward *= inverse_length;
        frame.move_right *= inverse_length;
    }

    pressed_    = 0;
    zoom_delta_ = 0.0f;

    return frame;
}

auto input_mapper::bindings() -> input_bindings& {
    return bindings_;
}

auto input_mapper::settings() -> input_settings& {
    return settings_;
}

}  // namespace vw::game
