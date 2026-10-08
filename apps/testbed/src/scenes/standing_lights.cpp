module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::testbed {

standing_lights_scene::standing_lights_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}
    , static_lights_{args.integer("--emitters", 400)}
    , dynamic_lights_{args.integer("--moving-lights", 64)}
    , hamlets_{std::max(args.integer("--hamlets", 24), 1)}
    , per_frame_{args.integer("--emitters-per-frame", 1)}
    , light_speed_{std::max(args.real("--light-speed", 1.0F), 0.0F)} {}

auto standing_lights_scene::spiral_point(int32 i, int32 count, float32 span) -> vec2f {
    constexpr float32 golden = 2.39996323f;

    const auto n    = static_cast<float32>(std::max(count, 1));
    const float32 t = static_cast<float32>(i) / n;
    const float32 r = span * std::sqrt(t);
    const float32 a = static_cast<float32>(i) * golden;

    return vec2f{r * std::cos(a), r * std::sin(a)};
}

auto standing_lights_scene::site(int32 i) const -> vec2i {
    const vec2f at =
        spiral_point(i, std::max(static_lights_, 1), static_cast<float32>(radius));

    return vec2i{
        static_cast<int32>(std::lround(at.x)),
        static_cast<int32>(std::lround(at.y)),
    };
}

auto standing_lights_scene::orbit_home(std::size_t) const -> vec2f {
    return vec2f{0.0f, 0.0f};
}

auto standing_lights_scene::orbit_radius(std::size_t, float32 spread) const -> float32 {
    return static_cast<float32>(radius) * (0.2f + (0.8f * spread));
}

auto standing_lights_scene::is_ready() const -> bool {
    return standing_;
}

auto standing_lights_scene::spawn_lights_() -> void {
    auto& world = stand().world();

    while (static_cast<int32>(lights_.size()) < dynamic_lights_) {
        lights_.push_back(
            world.create().with<ecs::transform_component>().with<ecs::light_component>().get_entity()
        );
    }
}

auto standing_lights_scene::place_emitters_() -> void {
    if (standing_) {
        return;
    }

    spawn_lights_();

    if (!seeded_) {
        pending_.reserve(static_cast<std::size_t>(std::max(static_lights_, 0)));
        for (int32 i = 0; i < static_lights_; ++i) {
            pending_.push_back(i);
        }
        seeded_ = true;
    }

    const int32 scale = stand().world_units_per_voxel();

    int32 done       = 0;
    std::size_t keep = 0;

    for (std::size_t i = 0; i < pending_.size(); ++i) {
        const int32 at_site = pending_[i];

        if (done < per_frame_) {
            const vec2i at = site(at_site);
            if (const auto surface = stand().grid().get_surface_voxel_y(at.x, at.y)) {
                stand().grid().set_voxel(
                    {at.x * scale, (*surface + 1) * scale, at.y * scale},
                    matter{voxels::amber[8], materials::lamp}
                );

                ++placed_;
                ++done;
                continue;
            }
        }

        pending_[keep++] = at_site;
    }

    pending_.resize(keep);

    if (!pending_.empty()) {
        return;
    }

    standing_ = true;

    log::info(
        "{}: {} emitters standing, {} moving lights", name(), placed_, lights_.size()
    );
}

auto standing_lights_scene::drive_lights_(float32 delta_time) -> void {
    if (lights_.empty()) {
        return;
    }

    if (stand().is_bench_ready()) {
        const uint32 visible = stand().renderer().get_visible_light_count();
        visible_peak_        = std::max(visible_peak_, visible);
        visible_sum_ += visible;
        ++visible_frames_;

        capped_frames_ +=
            (visible >= stand().renderer().get_max_visible_lights()) ? 1 : 0;
    }

    auto& world         = stand().world();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& light_sys     = world.system<ecs::light_system>();

    const auto& lamp = stand().renderer().get_block_light_settings();
    const auto scale = static_cast<float32>(stand().world_units_per_voxel());
    const auto phase = static_cast<float32>(phase_);

    phase_ += stand().benching()
        ? static_cast<float64>(light_speed_)
        : static_cast<float64>(delta_time * steps_per_second * light_speed_);

    for (std::size_t i = 0; i < lights_.size(); ++i) {
        const auto k      = static_cast<float32>(i);
        const auto spread = k / static_cast<float32>(lights_.size());

        const float32 speed = 0.004f + (0.002f * std::fmod(k, 5.0f));
        const float32 angle = (k * 1.7f) + (phase * speed);

        const vec2f home    = orbit_home(i);
        const float32 orbit = orbit_radius(i, spread);

        const float32 at_x = home.x + (orbit * std::cos(angle));
        const float32 at_z = home.y + (orbit * std::sin(angle));

        const auto vx = static_cast<int32>(std::lround(at_x));
        const auto vz = static_cast<int32>(std::lround(at_z));

        const auto surface = stand().grid().get_surface_voxel_y(vx, vz);
        const float32 y =
            surface ? (static_cast<float32>(*surface + 3) * scale) : stand().altitude();

        transform_sys.modify(lights_[i]).set_position({at_x * scale, y, at_z * scale});

        light_sys.modify(lights_[i])
            .set_color(vec3f{
                lamp.color.x * lamp.intensity,
                lamp.color.y * lamp.intensity,
                lamp.color.z * lamp.intensity,
            })
            .set_intensity(14.0f / 15.0f)
            .set_range(14.0f * round_reach * scale);
    }
}

auto standing_lights_scene::tick(float32 delta_time) -> void {
    place_emitters_();
    drive_lights_(delta_time);
}

auto standing_lights_scene::collect_report(gfx::report& out) const -> void {
    if (visible_frames_ == 0) {
        return;
    }

    const uint32 cap = stand().renderer().get_max_visible_lights();

    auto& section = out.section(name());

    section.value("emitters_placed", placed_)
        .value("emitters_asked", static_cast<int64>(static_lights_))
        .value("moving_lights", static_cast<uint64>(lights_.size()))
        .value("layout", layout_text())
        .value("radius_voxels", static_cast<int64>(radius))
        .value("visible_mean",
               static_cast<float64>(visible_sum_) / static_cast<float64>(visible_frames_), 1)
        .value("visible_peak", static_cast<uint64>(visible_peak_))
        .value("frames", visible_frames_);

    if (cap != gfx::light_buffer::no_cap) {
        section.value("frames_at_cap", static_cast<uint64>(capped_frames_))
            .value("cap", static_cast<uint64>(cap));
    }
}

auto standing_lights_scene::ui() -> void {
    ImGui::Text(
        "%s: %llu of %d emitters, %zu moving, %u visible", std::string{name()}.c_str(),
        static_cast<unsigned long long>(placed_), static_lights_, lights_.size(),
        stand().renderer().get_visible_light_count()
    );
}

auto clustered_lights_scene::centre_(int32 group) const -> vec2f {
    return spiral_point(group, hamlets(), static_cast<float32>(radius));
}

auto clustered_lights_scene::site(int32 i) const -> vec2i {
    const int32 groups = std::max(hamlets(), 1);

    const int32 group  = i % groups;
    const int32 member = i / groups;
    const int32 per    = (std::max(static_lights(), 1) + groups - 1) / groups;

    const vec2f centre = centre_(group);
    const vec2f offset = spiral_point(member, per, static_cast<float32>(hamlet_spread));

    return vec2i{
        static_cast<int32>(std::lround(centre.x + offset.x)),
        static_cast<int32>(std::lround(centre.y + offset.y)),
    };
}

auto clustered_lights_scene::orbit_home(std::size_t i) const -> vec2f {
    return centre_(static_cast<int32>(i) % std::max(hamlets(), 1));
}

auto clustered_lights_scene::orbit_radius(std::size_t, float32 spread) const -> float32 {
    return static_cast<float32>(hamlet_spread) * (0.3f + (0.7f * spread));
}

auto clustered_lights_scene::layout_text() const -> std::string {
    return std::format(
        "{} hamlets {} voxels across, spread over a spiral", hamlets(),
        hamlet_spread * 2
    );
}

}  // namespace vw::testbed
