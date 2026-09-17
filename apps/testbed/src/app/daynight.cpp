module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

auto testbed_app::tick_day_night_(float delta_time) -> void {
    if ((benching_ && !sun_in_bench_) || !day_night_running_) {
        return;
    }

    time_of_day_ += delta_time / std::max(1.0f, day_length_seconds_);
    time_of_day_ -= std::floor(time_of_day_);

    apply_time_of_day_();
}

auto testbed_app::apply_time_of_day_() -> void {
    const float32 angle = (time_of_day_ - 0.25f) * 2.0f * math::pi;

    const vec3f sun = math::normalize(
        vec3f{std::cos(angle), std::sin(angle), 0.42f}
    );

    const float32 height = std::clamp(sun.y, -1.0f, 1.0f);
    const float32 day    = std::clamp((height + 0.12f) / 0.35f, 0.0f, 1.0f);
    const float32 low    = 1.0f - std::clamp(std::abs(height) / 0.30f, 0.0f, 1.0f);

    auto& light = get_engine().get_renderer().get_directional_light_settings();

    light.direction = -sun;
    light.intensity = std::lerp(night_intensity_, 1.0f, day);

    const vec3f noon{1.0f, 0.97f, 0.92f};
    const vec3f dusk{1.0f, 0.62f, 0.34f};
    const vec3f night{0.42f, 0.52f, 0.78f};

    const vec3f warm{
        std::lerp(noon.x, dusk.x, low),
        std::lerp(noon.y, dusk.y, low),
        std::lerp(noon.z, dusk.z, low),
    };
    light.color = vec3f{
        std::lerp(night.x, warm.x, day),
        std::lerp(night.y, warm.y, day),
        std::lerp(night.z, warm.z, day),
    };

    const vec3f sky_day{0.40f, 0.60f, 0.90f};
    const vec3f sky_dusk{0.55f, 0.36f, 0.30f};
    const vec3f sky_night{0.03f, 0.04f, 0.09f};

    const vec3f sky_warm{
        std::lerp(sky_day.x, sky_dusk.x, low),
        std::lerp(sky_day.y, sky_dusk.y, low),
        std::lerp(sky_day.z, sky_dusk.z, low),
    };
    const vec3f sky{
        std::lerp(sky_night.x, sky_warm.x, day),
        std::lerp(sky_night.y, sky_warm.y, day),
        std::lerp(sky_night.z, sky_warm.z, day),
    };

    auto& renderer = get_engine().get_renderer();
    renderer.set_clear_color(sky.x, sky.y, sky.z, 1.0f);
    renderer.get_fog_settings().color = sky;

    auto& ambient = renderer.get_ambient_settings();
    ambient.sky = vec3f{
        (sky.x * 0.6f) + 0.020f,
        (sky.y * 0.6f) + 0.025f,
        (sky.z * 0.6f) + 0.040f,
    };

    ambient.ground = vec3f{
        std::lerp(0.030f, 0.20f, day),
        std::lerp(0.035f, 0.17f, day),
        std::lerp(0.055f, 0.14f, day),
    };
}

auto testbed_app::step_time_of_day_(float32 delta) -> void {
    time_of_day_ += delta;
    time_of_day_ -= std::floor(time_of_day_);
    apply_time_of_day_();
}

auto testbed_app::set_torch_(bool on) -> void {
    auto& world = get_engine().get_world();

    if (!on) {
        if (torch_.is_valid()) {
            world.destroy(torch_);
            torch_ = ecs::invalid_entity;
        }
        return;
    }

    if (torch_.is_valid()) {
        return;
    }

    torch_ = world.create()
                 .with<ecs::transform_component>()
                 .with<ecs::light_component>()
                 .get_entity();
}

auto testbed_app::tick_torch_(const vec3f& at) -> void {
    if (!torch_.is_valid()) {
        return;
    }

    auto& world = get_engine().get_world();
    world.system<ecs::transform_system>().modify(torch_).set_position(at);

    const auto& lamp = get_engine().get_renderer().get_block_light_settings();
    const auto scale = static_cast<float32>(generator_params_.world_units_per_voxel);

    world.system<ecs::light_system>()
        .modify(torch_)
        .set_color(vec3f{
            lamp.color.x * lamp.intensity,
            lamp.color.y * lamp.intensity,
            lamp.color.z * lamp.intensity,
        })
        .set_intensity(14.0f / 15.0f)
        .set_range(14.0f * round_reach * scale);
}

}  // namespace vw::testbed
