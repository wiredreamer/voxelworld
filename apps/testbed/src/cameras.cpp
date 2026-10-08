module vw.testbed;

import std;
import vw.core;
import vw.gfx;

namespace vw::testbed {
namespace {

constexpr log::log_category lc_{"cameras"};

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

const std::array<camera_entry, 6> camera_table{{
    entry_for<parked_rig>("parked"),
    entry_for<spin_rig>("spin"),
    entry_for<walk_rig>("walk"),
    entry_for<orbit_rig>("orbit"),
    entry_for<cave_rig>("cave"),
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

auto cave_rig::find_pocket_() const -> std::optional<vec3f> {
    const auto& grid  = stand().grid();
    const auto scale  = stand().world_units_per_voxel();
    const auto scalef = static_cast<float32>(scale);

    enum class cell : uint8 { unknown, air, solid };

    const auto cell_at = [&grid, scale](vec3i at) -> cell {
        const vec3i world{at.x * scale, at.y * scale, at.z * scale};
        if (!grid.has_chunk(grid.world_to_chunk_coord(world))) {
            return cell::unknown;
        }
        return grid.get_voxel(world).is_empty() ? cell::air : cell::solid;
    };

    const auto air_around = [&cell_at](vec3i at) -> bool {
        for (int32 dy = -clearance; dy <= clearance; ++dy) {
            for (int32 dz = -clearance; dz <= clearance; ++dz) {
                for (int32 dx = -clearance; dx <= clearance; ++dx) {
                    if (cell_at({at.x + dx, at.y + dy, at.z + dz}) != cell::air) {
                        return false;
                    }
                }
            }
        }
        return true;
    };

    static constexpr std::array<vec3i, 6> ways{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};

    const auto walled_in = [&cell_at](vec3i at) -> bool {
        return std::ranges::all_of(ways, [&](vec3i way) {
            for (int32 step = clearance + 1; step <= wall_reach; ++step) {
                const cell found = cell_at({
                    at.x + (way.x * step),
                    at.y + (way.y * step),
                    at.z + (way.z * step),
                });
                if (found != cell::air) {
                    return found == cell::solid;
                }
            }
            return false;
        });
    };

    for (int32 ring = 0; ring <= search_rings; ++ring) {
        for (int32 cz = -ring; cz <= ring; ++cz) {
            for (int32 cx = -ring; cx <= ring; ++cx) {
                if (std::max(std::abs(cx), std::abs(cz)) != ring) {
                    continue;
                }

                const vec3i column{cx * column_step, 0, cz * column_step};

                const auto surface = grid.get_surface_voxel_y(column.x, column.z);
                if (!surface) {
                    continue;
                }

                for (int32 y = *surface - min_depth; y > probe_bottom; y -= probe_step) {
                    const vec3i at{column.x, y, column.z};
                    if (cell_at(at) != cell::air || !air_around(at) || !walled_in(at)) {
                        continue;
                    }

                    return vec3f{
                        static_cast<float32>(at.x) * scalef,
                        static_cast<float32>(at.y) * scalef,
                        static_cast<float32>(at.z) * scalef,
                    };
                }
            }
        }
    }

    return std::nullopt;
}

auto cave_rig::drive(
    const camera_hint& hint, float32
) -> void {
    if (!pocket_ && stand().is_bench_ready()) {
        pocket_ = find_pocket_();
        if (pocket_) {
            log::info(lc_, "cave: standing in a pocket at {}, {}, {}", pocket_->x, pocket_->y, pocket_->z);
        } else if (!warned_) {
            log::warn(lc_, "cave: no walled pocket in reach, the camera stays above ground");
            warned_ = true;
        }
    }

    auto& camera = stand().camera();
    if (!pocket_) {
        camera.set_position(eye(stand(), hint));
        camera.set_rotation(hint.pitch, hint.yaw);
        return;
    }

    camera.set_position(*pocket_);
    camera.set_rotation(0.0f, static_cast<float32>(frame_++) * hint.degrees_per_frame);
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
