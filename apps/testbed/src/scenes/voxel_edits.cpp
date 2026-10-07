module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::testbed {

voxel_edits_scene::voxel_edits_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}, per_frame_{args.integer("--voxels-per-frame", 1)} {}

auto voxel_edits_scene::start_() -> void {
    const auto surface = stand().grid().get_surface_voxel_y(0, 0);
    if (!surface) {
        return;
    }

    top_voxel_ = *surface;
    started_   = true;

    mesh_base_ = stand().renderer().get_mesh_pool().get_gen_stats().chunks;
}

auto voxel_edits_scene::tick(float32) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    if (!started_) {
        start_();
        if (!started_) {
            return;
        }
    }

    const int32 scale = stand().world_units_per_voxel();

    for (int32 done = 0; done < per_frame_ && cursor_ < cells; ++cursor_) {
        const int32 x = (cursor_ % side) - (side / 2);
        const int32 z = ((cursor_ / side) % side) - (side / 2);
        const int32 y = top_voxel_ - (cursor_ / (side * side));

        const vec3i at{x * scale, y * scale, z * scale};

        if (stand().grid().get_voxel(at).is_empty()) {
            continue;
        }

        stand().grid().set_voxel(at, voxels::air);
        ++edits_;
        ++done;
    }
}

auto voxel_edits_scene::collect_report(gfx::report& out) const -> void {
    if (!started_ || edits_ == 0) {
        return;
    }

    const auto meshed = stand().renderer().get_mesh_pool().get_gen_stats().chunks - mesh_base_;

    const auto per = [this](uint64 n) -> float64 {
        return static_cast<float64>(n) / static_cast<float64>(edits_);
    };

    out.section(name())
        .value("voxels_removed", edits_)
        .value("chunk_meshes", meshed)
        .value("meshes_per_edit", per(meshed), 2)
        .value("box_side", static_cast<int64>(side))
        .value("voxels_per_frame", static_cast<int64>(per_frame_))
        .value("cursor", static_cast<int64>(cursor_))
        .value("cells", static_cast<int64>(cells));
}

auto voxel_edits_scene::ui() -> void {
    ImGui::Text("voxel-edits: %llu voxels removed, cursor %d of %d",
                static_cast<unsigned long long>(edits_), cursor_, cells);
}

}  // namespace vw::testbed
