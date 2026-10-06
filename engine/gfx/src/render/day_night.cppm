export module vw.gfx:render.day_night;

import std;

import vw.core;
import :renderer;

export namespace vw::gfx {

class day_night_cycle {
public:
    auto tick(float32 delta_time, renderer& target) -> void;
    auto apply(renderer& target) const -> void;
    auto step(float32 delta, renderer& target) -> void;
    auto draw_controls(renderer& target) -> void;

    [[nodiscard]] auto get_time_of_day() const -> float32 { return time_of_day_; }
    auto set_time_of_day(float32 time_of_day) -> void;

    [[nodiscard]] auto get_day_length_seconds() const -> float32 { return day_length_seconds_; }
    auto set_day_length_seconds(float32 seconds) -> void { day_length_seconds_ = seconds; }

    [[nodiscard]] auto is_running() const -> bool { return running_; }
    auto set_running(bool running) -> void { running_ = running; }

    [[nodiscard]] auto get_night_intensity() const -> float32 { return night_intensity_; }
    auto set_night_intensity(float32 intensity) -> void { night_intensity_ = intensity; }

    [[nodiscard]] auto hour() const -> int32;
    [[nodiscard]] auto minute() const -> int32;

private:
    float32 time_of_day_        = 0.5f;
    float32 day_length_seconds_ = 120.0f;
    float32 night_intensity_    = 0.06f;
    bool running_               = true;
};

}  // namespace vw::gfx
