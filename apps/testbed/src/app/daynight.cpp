module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

auto testbed_app::tick_day_night_(float delta_time) -> void {
    if (sun_pinned_ || (benching_ && !sun_in_bench_)) {
        return;
    }

    day_night_.tick(delta_time, get_engine().get_renderer());
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
