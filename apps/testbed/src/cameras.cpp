module vw.testbed;

import std;
import vw.core;
import vw.gfx;

namespace vw::testbed {
namespace {

auto eye(const testbed_app& stand, const camera_hint& hint) -> vec3f {
    return vec3f{hint.offset.x, stand.altitude() + hint.offset.y, hint.offset.z};
}

struct camera_entry {
    std::string_view name;
    auto (*build)(testbed_app&) -> std::unique_ptr<camera_rig>;
};

template <typename Rig>
constexpr auto entry_for(std::string_view name) -> camera_entry {
    return {name, [](testbed_app& stand) -> std::unique_ptr<camera_rig> {
                return std::make_unique<Rig>(stand);
            }};
}

const std::array<camera_entry, 5> camera_table{{
    entry_for<parked_rig>("parked"),
    entry_for<spin_rig>("spin"),
    entry_for<walk_rig>("walk"),
    entry_for<orbit_rig>("orbit"),
    entry_for<free_rig>("free"),
}};

}  // namespace

auto parked_rig::drive(
    const camera_hint& hint, float32
) -> void {
    auto& camera = stand().camera();
    camera.set_position(eye(stand(), hint));
    camera.set_rotation(hint.pitch, hint.yaw);
}

auto spin_rig::drive(
    const camera_hint& hint, float32
) -> void {
    auto& camera = stand().camera();
    camera.set_position(eye(stand(), hint));
    camera.set_rotation(hint.pitch, static_cast<float32>(frame_++) * hint.degrees_per_frame);
}

auto walk_rig::drive(
    const camera_hint& hint, float32
) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    const auto from = eye(stand(), hint);

    auto& camera = stand().camera();
    camera.set_position({
        from.x + (static_cast<float32>(frame_++) * per_frame),
        from.y + path_clearance,
        from.z,
    });
    camera.set_rotation(hint.pitch, 90.0f);
}

auto orbit_rig::drive(
    const camera_hint& hint, float32
) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    const float32 angle = static_cast<float32>(frame_++) * hint.degrees_per_frame;
    const float32 rad   = math::radians(angle);

    const auto from = eye(stand(), hint);

    auto& camera = stand().camera();
    camera.set_position({
        from.x + (std::sin(rad) * radius),
        from.y + path_clearance,
        from.z + (std::cos(rad) * radius),
    });
    camera.set_rotation(hint.pitch, angle + 90.0f);
}

auto free_rig::drive(
    const camera_hint&, float32 delta_time
) -> void {
    stand().camera_controller().update(delta_time);
}

auto find_camera(
    std::string_view name
) -> std::optional<camera_factory> {
    const auto found = std::ranges::find(camera_table, name, &camera_entry::name);
    if (found == camera_table.end()) {
        return std::nullopt;
    }

    return camera_factory{found->build};
}

auto camera_names() -> std::vector<std::string_view> {
    std::vector<std::string_view> names;
    names.reserve(camera_table.size());
    for (const auto& entry : camera_table) {
        names.push_back(entry.name);
    }
    return names;
}

}  // namespace vw::testbed
