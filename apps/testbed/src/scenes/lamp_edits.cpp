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
    const auto& wgs      = stand().world().system<ecs::world_grid_system>();
    const auto& mesh_gen = stand().renderer().get_mesh_pool().get_gen_stats();

    started_    = true;
    mesh_base_  = mesh_gen.chunks;
    quads_base_ = mesh_gen.quads;

    quads_per_chunk_base_ = mesh_gen.chunks == 0
        ? 0.0
        : static_cast<float64>(mesh_gen.quads) / static_cast<float64>(mesh_gen.chunks);

    relight_base_     = wgs.get_stats().relit_columns;
    relit_chunk_base_ = wgs.get_stats().relit_chunks;

    const auto light_stats = wgs.get_light_stats();
    columns_base_          = light_stats.columns;
    flood_base_ms_         = light_stats.flood_ms;
    bake_base_ms_          = light_stats.bake_ms;
}

auto lamp_edits_scene::tick(float32 /*delta_time*/) -> void {
    if (!stand().is_bench_ready()) {
        return;
    }

    if (!started_) {
        start_();
    }

    const int32 scale = stand().voxel_scale();

    // Один эмиттер за шаг по сетке над поверхностью, с таким шагом, чтобы лужи
    // света перекрывались: лампа достаёт на четырнадцать вокселей, а стоят они
    // через четыре, поэтому каждая поверхность в квадрате оказывается внутри
    // чьего-нибудь градиента. Лампа с чистой землёй вокруг оценила бы лучший
    // случай, а освещённое подземелье выглядит не так.
    for (int32 done = 0; done < per_frame_ && cursor_ < cells; ++cursor_) {
        const int32 ix = cursor_ % side;
        const int32 iz = cursor_ / side;

        const int32 vx = (ix - (side / 2)) * spacing;
        const int32 vz = (iz - (side / 2)) * spacing;

        const auto surface = stand().grid().get_surface_y(vx, vz);
        if (!surface) {
            continue;
        }

        // На воксель над землёй, чтобы блок всегда ложился в воздух и оба
        // прогона делали одну и ту же геометрическую работу, каким бы блок ни
        // оказался.
        stand().grid().set_voxel(
            {vx * scale, (*surface + 1) * scale, vz * scale},
            voxel{inert_ ? blocks::terrain::stone[1] : blocks::terrain::glowstone}
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

    const auto& wgs     = stand().world().system<ecs::world_grid_system>();
    const auto& stats   = wgs.get_stats();
    const auto relit    = stats.relit_columns - relight_base_;
    const auto relit_ch = stats.relit_chunks - relit_chunk_base_;

    const auto light_stats = wgs.get_light_stats();
    const auto columns     = light_stats.columns - columns_base_;
    const auto flood_ms    = light_stats.flood_ms - flood_base_ms_;
    const auto bake_ms     = light_stats.bake_ms - bake_base_ms_;

    const auto per = [this](uint64 n) -> float64 {
        return static_cast<float64>(n) / static_cast<float64>(placed_);
    };

    const auto us_a_column = [columns](float32 ms) -> float64 {
        return columns == 0
            ? 0.0
            : (static_cast<float64>(ms) * 1000.0) / static_cast<float64>(columns);
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
        .value("relight_columns", relit)
        .value("relight_columns_per_edit", per(relit))
        .value("columns_flooded", columns)
        .value("chunks_changed", relit_ch)
        .value("quads_built", quads)
        .value("quads_per_chunk", quads_a_chunk, 0)
        .value("quads_per_chunk_streaming", quads_per_chunk_base_, 0)
        .value("flood_us_per_column", us_a_column(flood_ms), 0)
        .value("bake_us_per_column", us_a_column(bake_ms), 0)
        .value("relight_backlog", static_cast<uint64>(stats.relight_backlog));
}

auto lamp_edits_scene::ui() -> void {
    ImGui::Text("lamp-edits: %llu %s placed, cursor %d of %d",
                static_cast<unsigned long long>(placed_), inert_ ? "inert blocks" : "lamps",
                cursor_, cells);
}

}  // namespace vw::testbed
