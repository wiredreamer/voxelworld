module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

[[nodiscard]] auto testbed_app::pick_voxel_() const -> std::optional<voxel_pick> {
    if (world_grid_ == nullptr) {
        return std::nullopt;
    }

    const auto scale   = static_cast<float32>(generator_params_.world_units_per_voxel);
    const auto& camera = get_engine().get_camera();

    const vec3f eye = camera.get_position();
    const vec3f dir = camera.get_forward();
    const vec3f origin{eye.x / scale, eye.y / scale, eye.z / scale};

    vec3i at{
        static_cast<int32>(std::floor(origin.x)),
        static_cast<int32>(std::floor(origin.y)),
        static_cast<int32>(std::floor(origin.z)),
    };

    constexpr float32 far_away = std::numeric_limits<float32>::max();

    vec3i step{};
    vec3f next{};
    vec3f span{};

    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float32 d = dir[axis];

        if (std::abs(d) < 1e-6f) {
            step[axis] = 0;
            next[axis] = far_away;
            span[axis] = far_away;
            continue;
        }

        step[axis] = d > 0.0f ? 1 : -1;
        span[axis] = std::abs(1.0f / d);

        const auto edge = static_cast<float32>(at[axis]) + (d > 0.0f ? 1.0f : 0.0f);
        next[axis]      = (edge - origin[axis]) / d;
    }

    vec3i empty = at;

    for (int32 i = 0; i < reach_voxels_; ++i) {
        const vec3i world{
            at.x * generator_params_.world_units_per_voxel,
            at.y * generator_params_.world_units_per_voxel,
            at.z * generator_params_.world_units_per_voxel,
        };

        if (!world_grid_->get_voxel(world).is_empty()) {
            return voxel_pick{.solid_voxel_pos = at, .empty_voxel_pos = empty};
        }

        empty = at;

        if (next.x < next.y && next.x < next.z) {
            at.x += step.x;
            next.x += span.x;
        } else if (next.y < next.z) {
            at.y += step.y;
            next.y += span.y;
        } else {
            at.z += step.z;
            next.z += span.z;
        }
    }

    return std::nullopt;
}

auto testbed_app::update_hovered_() -> void {
    hovered_.reset();

    if (tool_ == edit_tool::none || !camera_controller_->is_mouse_captured()) {
        return;
    }

    hovered_ = pick_voxel_();
}

auto testbed_app::draw_hover_() -> void {
    if (!hovered_) {
        return;
    }

    const auto scale = static_cast<float32>(generator_params_.world_units_per_voxel);
    auto& renderer   = get_engine().get_renderer();

    const auto outline = [&](vec3i cell, color clr) {
        const vec3f at{
            (static_cast<float32>(cell.x) * scale) - 0.05f,
            (static_cast<float32>(cell.y) * scale) - 0.05f,
            (static_cast<float32>(cell.z) * scale) - 0.05f,
        };
        renderer.draw_box(at, vec3f{scale + 0.1f, scale + 0.1f, scale + 0.1f}, clr);
    };

    outline(hovered_->solid_voxel_pos, colors::white);

    if (tool_ == edit_tool::place && hovered_->empty_voxel_pos != hovered_->solid_voxel_pos) {
        outline(hovered_->empty_voxel_pos, colors::green_8);
    }
}

auto testbed_app::apply_tool_() -> void {
    if (tool_ == edit_tool::none || world_grid_ == nullptr) {
        return;
    }

    update_hovered_();
    if (!hovered_) {
        return;
    }

    const int32 scale = generator_params_.world_units_per_voxel;

    if (tool_ == edit_tool::state) {
        const vec3i& held = hovered_->solid_voxel_pos;
        world_grid_->set_state(
            {held.x * scale, held.y * scale, held.z * scale},
            voxel_state{static_cast<uint8>(state_bits_)}
        );
        ++edit_clicks_;
        return;
    }

    const bool placing = tool_ == edit_tool::place;
    const vec3i cell   = placing ? hovered_->empty_voxel_pos : hovered_->solid_voxel_pos;

    world_grid_->set_voxel(
        {cell.x * scale, cell.y * scale, cell.z * scale},
        placing ? voxel_menu[static_cast<std::size_t>(place_choice_)].id : matter{}
    );

    ++edit_clicks_;
}

auto testbed_app::drop_emitter(matter id, int32 radius) -> void {
    const int32 scale = generator_params_.world_units_per_voxel;

    const auto floor_div = [](int32 a, int32 b) -> int32 {
        return a >= 0 ? a / b : (a - b + 1) / b;
    };

    const auto pos = get_engine().get_camera().get_position();
    const int32 vx = floor_div(static_cast<int32>(std::floor(pos.x)), scale);
    const int32 vz = floor_div(static_cast<int32>(std::floor(pos.z)), scale);

    const auto surface = world_grid_->get_surface_voxel_y(vx, vz);
    if (!surface) {
        drop_status_ = std::format("no ground under voxel {},{}", vx, vz);
        return;
    }

    for (int32 dy = -radius; dy <= radius; ++dy) {
        for (int32 dz = -radius; dz <= radius; ++dz) {
            for (int32 dx = -radius; dx <= radius; ++dx) {
                world_grid_->set_voxel(
                    {(vx + dx) * scale, (*surface + dy) * scale, (vz + dz) * scale},
                    id
                );
            }
        }
    }

    const int32 side = (2 * radius) + 1;
    drop_status_ =
        std::format("{} voxels at {},{},{}", side * side * side, vx, *surface, vz);
}

}  // namespace vw::testbed
