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
    const auto surface = stand().grid().get_surface_y(0, 0);
    if (!surface) {
        return;
    }

    // get_surface_y отвечает в вокселях, и top_voxel_ тоже. Здесь когда-то
    // делили на масштаб вокселя, отчего лопата оказывалась на восьмой части
    // высоты мира — глубоко в породе, где ничего не освещено и ничего не
    // рисуется. Сцена мерила копку, которая ни разу не вышла на поверхность.
    top_voxel_ = *surface;
    started_   = true;

    mesh_base_ = stand().renderer().get_mesh_pool().get_gen_stats().chunks;

    const auto& wgs   = stand().world().system<ecs::world_grid_system>();
    relight_base_     = wgs.get_stats().relit_columns;
    relit_chunk_base_ = wgs.get_stats().relit_chunks;
    light_base_       = wgs.get_light_stats().columns;
}

auto voxel_edits_scene::tick(float32 /*delta_time*/) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    if (!started_) {
        start_();
        if (!started_) {
            return;
        }
    }

    const int32 scale = stand().voxel_scale();

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

    const auto& wgs     = stand().world().system<ecs::world_grid_system>();
    const auto& stats   = wgs.get_stats();
    const auto relit    = stats.relit_columns - relight_base_;
    const auto relit_ch = stats.relit_chunks - relit_chunk_base_;
    const auto lit      = wgs.get_light_stats().columns - light_base_;

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
        .value("cells", static_cast<int64>(cells))
        .value("relight_columns", relit)
        .value("relight_columns_per_edit", per(relit))
        .value("columns_flooded", lit)
        .value("chunks_changed", relit_ch)
        .value("relight_backlog", static_cast<uint64>(stats.relight_backlog));
}

auto voxel_edits_scene::ui() -> void {
    ImGui::Text("voxel-edits: %llu voxels removed, cursor %d of %d",
                static_cast<unsigned long long>(edits_), cursor_, cells);
}

}  // namespace vw::testbed
