module;

#include <imgui.h>

module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

auto debug_window::render_systems_panel() -> void {
    const auto& world  = engine_->get_world();
    const auto& update = world.get_update_stats();

    metric_row("world update", update.total_ms);
    ImGui::Separator();

    for (std::size_t i = 0; i < ecs::world_system_count; ++i) {
        const auto name = ecs::world_system_names[i];
        metric_row(name.data(), update.ms[i]);

        if (name == ecs::physics_system::system_name) {
            const auto& physics = world.system<ecs::physics_system>().get_stats();
            ImGui::Indent();
            metric_row("step", physics.step_ms);
            metric_row("voxel_col", physics.voxel_collision_ms);
            metric_row("entity_col", physics.entity_collision_ms);
            metric_row("query", physics.entity_query_ms);
            metric_row("resolve", physics.entity_resolve_ms);
            ImGui::Text(
                "%-14s %6d  steps %d", "query hits", physics.entity_query_results,
                physics.step_count
            );
            ImGui::Unindent();
        } else if (name == ecs::world_grid_system::system_name) {
            const auto& grid = world.system<ecs::world_grid_system>().get_stats();
            ImGui::Indent();
            metric_row("integrate", grid.integrate_ms);
            metric_row("stage", grid.stage_ms);
            metric_row("boundary", grid.boundary_from_ms);
            metric_row("chunk_create", grid.chunk_create_ms);
            metric_row("requests", grid.request_columns_ms);
            metric_row("rebuild", grid.rebuild_active_ms);
            metric_row("unload", grid.unload_ms);
            metric_row("light_apply", grid.light_apply_ms);
            ImGui::Unindent();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("reset##systems")) {
        metric_max_.clear();
    }
}

auto debug_window::render_render_panel() -> void {
    const auto& timing = engine_->get_renderer().get_stats().timing;
    const auto& eng    = engine_->get_stats();

    metric_row("begin_frame", eng.begin_frame_ms);
    metric_row("app_render", eng.app_render_ms);
    metric_row("mesh_sync", timing.mesh_sync_ms);
    metric_row("shadow_update", timing.shadow_map_update_ms);
    metric_row("buffer_pool", timing.buffer_pool_update_ms);

    const auto& combined = engine_->get_renderer().get_stats().combined_buffers;
    ImGui::Indent();
    metric_row("destroyed", combined.timing.destroyed_ms);
    metric_row("meshes", combined.timing.meshes_ms);
    metric_row("transforms", combined.timing.transforms_ms);
    metric_row("staging", combined.timing.staging_flush_ms);

    ImGui::Text(
        "%-16s %6u mesh  %6u transform", "pending", combined.mesh_pending,
        combined.transform_pending
    );
    ImGui::Unindent();

    metric_row("compute_cull", timing.compute_cull_ms);
    metric_row("light_gather", timing.light_gather_ms);
    metric_row("light_cull", timing.light_cull_ms);
    metric_row("shadow_pass", timing.shadow_pass_ms);
    metric_row("world_pass", timing.world_pass_ms);
    ImGui::Indent();
    metric_row("uniform", timing.world_pass_uniform_ms);
    metric_row("geometry", timing.world_pass_geometry_ms);
    metric_row("debug", timing.world_pass_debug_ms);
    metric_row("imgui", timing.world_pass_imgui_ms);
    ImGui::Unindent();
    metric_row("end_frame", eng.end_frame_ms);

    ImGui::Separator();
    if (timing.gpu.supported) {
        ImGui::TextUnformatted("gpu (execution)");
        for (uint32 stage = 0; stage < gpu_stage_count; ++stage) {
            metric_row(gpu_stage_names[stage].data(), timing.gpu.ms[stage]);
        }
    } else {
        ImGui::TextUnformatted("gpu timestamps unsupported");
    }

    ImGui::Spacing();
    if (ImGui::Button("reset##render")) {
        metric_max_.clear();
    }
}

auto debug_window::render_buffers_panel() -> void {
    const auto& stats = engine_->get_renderer().get_stats().combined_buffers;

    ImGui::Text(
        "quad load avg %.2f min %.2f max %.2f", stats.quad_load_avg, stats.quad_load_min,
        stats.quad_load_max
    );
    ImGui::Text(
        "mesh %u/%u instance %u/%u", stats.mesh_count, stats.mesh_capacity,
        stats.instance_count, stats.instance_capacity
    );
    ImGui::Separator();

    for (std::size_t i = 0; i < stats.buffers.size(); ++i) {
        const auto& buffer = stats.buffers[i];
        ImGui::Text("Buffer %zu:", i);
        ImGui::Indent();
        ImGui::Text("chunk_size: %u quads", buffer.chunk_size.quad_count);
        ImGui::Text(
            "quad load: avg %.2f min %.2f max %.2f", buffer.quad_load_avg,
            buffer.quad_load_min, buffer.quad_load_max
        );
        ImGui::Text(
            "mesh %u/%u instance %u/%u", buffer.mesh_count, buffer.mesh_capacity,
            buffer.instance_count, buffer.instance_capacity
        );
        ImGui::Unindent();
        ImGui::Separator();
    }
}

auto debug_window::render_world_panel() -> void {
    const auto position = engine_->get_camera().get_position();
    ImGui::Text("camera  %.1f %.1f %.1f", position.x, position.y, position.z);

    const auto& system = engine_->get_world().system<ecs::world_grid_system>();
    const auto* grid   = system.grid();
    if (grid == nullptr) {
        ImGui::TextUnformatted("no world grid");
        return;
    }

    const auto chunk = grid->world_to_chunk_coord({
        static_cast<int32>(position.x),
        static_cast<int32>(position.y),
        static_cast<int32>(position.z),
    });
    ImGui::Text("chunk   %d %d %d", chunk.x, chunk.y, chunk.z);

    ImGui::SeparatorText("streaming");
    const auto& stats = system.get_stats();
    count_row("columns", grid->column_count());
    count_row("active", stats.active_count);
    count_row("pending", stats.pending_count);
    count_row("staged", stats.staged_count);
    count_row("lighting", stats.lighting_count);
    count_row("chunks", grid->chunk_count());
    count_row("drawn", grid->drawn_chunk_count());
    count_row("mesh pending", engine_->get_renderer().get_mesh_pool().get_pending_count());

    ImGui::SeparatorText("relight");
    count_row("backlog", stats.relight_backlog);
    ImGui::Text("%-18s %8llu", "columns", static_cast<unsigned long long>(stats.relit_columns));
    ImGui::Text("%-18s %8llu", "chunks", static_cast<unsigned long long>(stats.relit_chunks));

    ImGui::SeparatorText("terrain workers");
    const auto loader = system.get_loader_stats();
    ImGui::Text(
        "%llu columns, %llu chunks", static_cast<unsigned long long>(loader.columns),
        static_cast<unsigned long long>(loader.chunks)
    );
    ImGui::Text(
        "mean %.0fus p50 %.0fus p99 %.0fus max %.0fus", loader.mean_us, loader.p50_us,
        loader.p99_us, loader.max_us
    );
    ImGui::Text("queue %u, peak %u", loader.queue_depth, loader.queue_peak);

    ImGui::SeparatorText("light workers");
    const auto light = system.get_light_stats();
    ImGui::Text("%llu columns", static_cast<unsigned long long>(light.columns));
    ImGui::Text(
        "rows %.1fms flood %.1fms bake %.1fms", light.rows_ms, light.flood_ms, light.bake_ms
    );
    ImGui::Text(
        "mean %.0fus p50 %.0fus p99 %.0fus max %.0fus", light.mean_us, light.p50_us,
        light.p99_us, light.max_us
    );
    ImGui::Text("queue %u, peak %u", light.queue_depth, light.queue_peak);

    ImGui::SeparatorText("mesh workers");
    const auto mesh = engine_->get_renderer().get_mesh_pool().get_gen_stats();
    ImGui::Text(
        "%llu chunks, %llu quads", static_cast<unsigned long long>(mesh.chunks),
        static_cast<unsigned long long>(mesh.quads)
    );
    ImGui::Text(
        "mean %.0fus p50 %.0fus p99 %.0fus max %.0fus", mesh.mean_us, mesh.p50_us, mesh.p99_us,
        mesh.max_us
    );
    ImGui::Text("queue %u, peak %u", mesh.queue_depth, mesh.queue_peak);

    ImGui::SeparatorText("chunk cull");
    const auto& cull = engine_->get_renderer().get_stats().combined_buffers.chunk_cull;
    ImGui::Text(
        "%s, %u of %u visible",
        engine_->get_renderer().is_chunk_cull_enabled() ? "on" : "off", cull.visible,
        cull.chunks
    );
    ImGui::Text("walk %.2f ms", cull.walk_ms);
    ImGui::Text(
        "visited %u (%u empty), sealed %u", cull.visited, cull.visited_empty, cull.sealed
    );
    ImGui::Text(
        "links %u, merged %u, pockets %u", cull.known_links, cull.merged, cull.max_pockets
    );
}

}  // namespace vw::gfx
