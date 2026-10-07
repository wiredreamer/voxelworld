module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::testbed {

lamp_edits_scene::lamp_edits_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}
    , per_frame_{args.integer("--lamps-per-frame", 1)}
    , inert_{args.flag("--inert")} {}

auto lamp_edits_scene::start_() -> void {
    const auto& mesh_gen = stand().renderer().get_mesh_pool().get_gen_stats();

    started_    = true;
    mesh_base_  = mesh_gen.chunks;
    quads_base_ = mesh_gen.quads;

    quads_per_chunk_base_ = mesh_gen.chunks == 0
        ? 0.0
        : static_cast<float64>(mesh_gen.quads) / static_cast<float64>(mesh_gen.chunks);
}

auto lamp_edits_scene::tick(float32) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    if (!started_) {
        start_();
    }

    const int32 scale = stand().world_units_per_voxel();

    for (int32 done = 0; done < per_frame_ && cursor_ < cells; ++cursor_) {
        const int32 ix = cursor_ % side;
        const int32 iz = cursor_ / side;

        const int32 vx = (ix - (side / 2)) * spacing;
        const int32 vz = (iz - (side / 2)) * spacing;

        const auto surface = stand().grid().get_surface_voxel_y(vx, vz);
        if (!surface) {
            continue;
        }

        stand().grid().set_voxel(
            {vx * scale, (*surface + 1) * scale, vz * scale},
            inert_ ? voxels::gray[10] : voxels::lamp_amber
        );

        ++placed_;
        ++done;
    }
}

auto lamp_edits_scene::collect_report(gfx::report& out) const -> void {
    if (!started_ || placed_ == 0) {
        return;
    }

    const auto& mesh_gen = stand().renderer().get_mesh_pool().get_gen_stats();
    const auto meshed    = mesh_gen.chunks - mesh_base_;
    const auto quads     = mesh_gen.quads - quads_base_;

    const auto per = [this](uint64 n) -> float64 {
        return static_cast<float64>(n) / static_cast<float64>(placed_);
    };

    const float64 quads_a_chunk =
        meshed == 0 ? 0.0 : static_cast<float64>(quads) / static_cast<float64>(meshed);

    out.section(name())
        .value("placed", placed_)
        .value("block", inert_ ? "inert" : "lamp")
        .value("chunk_meshes", meshed)
        .value("meshes_per_edit", per(meshed), 2)
        .value("grid_side", static_cast<int64>(side))
        .value("spacing", static_cast<int64>(spacing))
        .value("per_frame", static_cast<int64>(per_frame_))
        .value("cursor", static_cast<int64>(cursor_))
        .value("cells", static_cast<int64>(cells))
        .value("quads_built", quads)
        .value("quads_per_chunk", quads_a_chunk, 0)
        .value("quads_per_chunk_streaming", quads_per_chunk_base_, 0);
}

auto lamp_edits_scene::ui() -> void {
    ImGui::Text("lamp-edits: %llu %s placed, cursor %d of %d",
                static_cast<unsigned long long>(placed_), inert_ ? "inert voxels" : "lamps",
                cursor_, cells);
}

}  // namespace vw::testbed
