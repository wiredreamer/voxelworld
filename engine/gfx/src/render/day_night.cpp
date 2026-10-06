module;

#include <imgui.h>

module vw.gfx;

import std;
import vw.core;

namespace vw::gfx {

namespace {

auto mixed(const vec3f& from, const vec3f& to, float32 t) -> vec3f {
    return vec3f{
        std::lerp(from.x, to.x, t),
        std::lerp(from.y, to.y, t),
        std::lerp(from.z, to.z, t),
    };
}

}  // namespace

auto day_night_cycle::tick(
    float32 delta_time, renderer& target
) -> void {
    if (running_) {
        set_time_of_day(time_of_day_ + (delta_time / std::max(1.0f, day_length_seconds_)));
    }
    apply(target);
}

auto day_night_cycle::step(
    float32 delta, renderer& target
) -> void {
    set_time_of_day(time_of_day_ + delta);
    apply(target);
}

auto day_night_cycle::draw_controls(
    renderer& target
) -> void {
    ImGui::Text("%02d:%02d %s", hour(), minute(), running_ ? "" : "(paused)");

    float32 time = time_of_day_;
    if (ImGui::SliderFloat("Time", &time, 0.0f, 1.0f, "%.3f")) {
        set_time_of_day(time);
        apply(target);
    }
    ImGui::SliderFloat("Day (s)", &day_length_seconds_, 8.0f, 600.0f, "%.0f");
    ImGui::Checkbox("Sun moves", &running_);
}

auto day_night_cycle::set_time_of_day(
    float32 time_of_day
) -> void {
    time_of_day_ = time_of_day - std::floor(time_of_day);
}

auto day_night_cycle::hour() const -> int32 {
    return static_cast<int32>(time_of_day_ * 24.0f);
}

auto day_night_cycle::minute() const -> int32 {
    return static_cast<int32>(((time_of_day_ * 24.0f) - static_cast<float32>(hour())) * 60.0f);
}

auto day_night_cycle::apply(
    renderer& target
) const -> void {
    const float32 angle = (time_of_day_ - 0.25f) * 2.0f * math::pi;

    const vec3f sun = math::normalize(vec3f{std::cos(angle), std::sin(angle), 0.42f});

    const float32 height = std::clamp(sun.y, -1.0f, 1.0f);
    const float32 day    = std::clamp((height + 0.12f) / 0.35f, 0.0f, 1.0f);
    const float32 low    = 1.0f - std::clamp(std::abs(height) / 0.30f, 0.0f, 1.0f);

    auto& light = target.get_directional_light_settings();

    light.direction = -sun;
    light.intensity = std::lerp(night_intensity_, 1.0f, day);

    const vec3f noon{1.0f, 0.97f, 0.92f};
    const vec3f dusk{1.0f, 0.62f, 0.34f};
    const vec3f night{0.42f, 0.52f, 0.78f};

    light.color = mixed(night, mixed(noon, dusk, low), day);

    const vec3f sky_day{0.40f, 0.60f, 0.90f};
    const vec3f sky_dusk{0.55f, 0.36f, 0.30f};
    const vec3f sky_night{0.03f, 0.04f, 0.09f};

    const vec3f sky = mixed(sky_night, mixed(sky_day, sky_dusk, low), day);

    target.set_clear_color(sky.x, sky.y, sky.z, 1.0f);
    target.get_fog_settings().color = sky;

    auto& ambient = target.get_ambient_settings();
    ambient.sky   = vec3f{
        (sky.x * 0.6f) + 0.020f,
        (sky.y * 0.6f) + 0.025f,
        (sky.z * 0.6f) + 0.040f,
    };

    ambient.ground = mixed(vec3f{0.030f, 0.035f, 0.055f}, vec3f{0.20f, 0.17f, 0.14f}, day);
}

}  // namespace vw::gfx
