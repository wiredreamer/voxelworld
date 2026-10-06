module vw.gfx;

import std;
import vw.core;
import vw.world;
import :engine.frame_recorder;

namespace vw::gfx {

namespace {

struct stage_desc {
    std::string_view name;
    auto (*get)(const frame_sample&) -> float32;
};

template <gpu_stage Stage>
constexpr auto gpu_stage_desc() -> stage_desc {
    return {
        gpu_stage_names[static_cast<uint32>(Stage)],
        [](const frame_sample& s) -> float32 {
            return s.render.gpu.ms[static_cast<uint32>(Stage)];
        },
    };
}

constexpr std::array cpu_stages{
    stage_desc{"frame", [](const frame_sample& s) -> float32 { return s.engine.frame_ms; }},
    stage_desc{"world_update", [](const frame_sample& s) -> float32 { return s.engine.world_update_ms; }},
    stage_desc{"world_render", [](const frame_sample& s) -> float32 { return s.engine.world_render_ms; }},
    stage_desc{"begin_frame", [](const frame_sample& s) -> float32 { return s.engine.begin_frame_ms; }},
    stage_desc{"app_render", [](const frame_sample& s) -> float32 { return s.engine.app_render_ms; }},
    stage_desc{"renderer", [](const frame_sample& s) -> float32 { return s.engine.renderer_ms; }},
    stage_desc{"end_frame", [](const frame_sample& s) -> float32 { return s.engine.end_frame_ms; }},
    stage_desc{"mesh_sync", [](const frame_sample& s) -> float32 { return s.render.mesh_sync_ms; }},
    stage_desc{"shadow_map_update", [](const frame_sample& s) -> float32 { return s.render.shadow_map_update_ms; }},
    stage_desc{"buffer_pool_update", [](const frame_sample& s) -> float32 { return s.render.buffer_pool_update_ms; }},
    stage_desc{"compute_cull", [](const frame_sample& s) -> float32 { return s.render.compute_cull_ms; }},
    stage_desc{"light_gather", [](const frame_sample& s) -> float32 { return s.render.light_gather_ms; }},
    stage_desc{"grass_prepare", [](const frame_sample& s) -> float32 { return s.render.grass_prepare_ms; }},
    stage_desc{"light_cull", [](const frame_sample& s) -> float32 { return s.render.light_cull_ms; }},
    stage_desc{"shadow_pass", [](const frame_sample& s) -> float32 { return s.render.shadow_pass_ms; }},
    stage_desc{"world_pass", [](const frame_sample& s) -> float32 { return s.render.world_pass_ms; }},
    stage_desc{"world_pass_uniform", [](const frame_sample& s) -> float32 { return s.render.world_pass_uniform_ms; }},
    stage_desc{"world_pass_geometry", [](const frame_sample& s) -> float32 { return s.render.world_pass_geometry_ms; }},
    stage_desc{"world_pass_debug", [](const frame_sample& s) -> float32 { return s.render.world_pass_debug_ms; }},
    stage_desc{"world_pass_imgui", [](const frame_sample& s) -> float32 { return s.render.world_pass_imgui_ms; }},
    stage_desc{"cascades drawn", [](const frame_sample& s) -> float32 { return s.render.shadow_cascades_drawn; }},
    stage_desc{"grid_stage", [](const frame_sample& s) -> float32 { return s.grid.stage_ms; }},
    stage_desc{"grid_integrate", [](const frame_sample& s) -> float32 { return s.grid.integrate_ms; }},
    stage_desc{"grid_boundary_from", [](const frame_sample& s) -> float32 { return s.grid.boundary_from_ms; }},
    stage_desc{"grid_chunk_create", [](const frame_sample& s) -> float32 { return s.grid.chunk_create_ms; }},
    stage_desc{"grid_requests", [](const frame_sample& s) -> float32 { return s.grid.request_columns_ms; }},
    stage_desc{"grid_pending", [](const frame_sample& s) -> float32 { return static_cast<float32>(s.grid.pending_count); }},
    stage_desc{"grid_staged", [](const frame_sample& s) -> float32 { return static_cast<float32>(s.grid.staged_count); }},
};

template <std::size_t I>
constexpr auto system_stage_desc() -> stage_desc {
    return {
        ecs::world_system_names[I],
        [](const frame_sample& s) -> float32 { return s.systems.ms[I]; },
    };
}

constexpr auto system_stages = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::array<stage_desc, sizeof...(Is)>{system_stage_desc<Is>()...};
}(std::make_index_sequence<ecs::world_system_count>{});

constexpr std::array gpu_stages{
    gpu_stage_desc<gpu_stage::frame>(),
    gpu_stage_desc<gpu_stage::buffer_upload>(),
    gpu_stage_desc<gpu_stage::compute_cull>(),
    gpu_stage_desc<gpu_stage::light_cull>(),
    gpu_stage_desc<gpu_stage::shadow_pass>(),
    gpu_stage_desc<gpu_stage::shadow_cascade_0>(),
    gpu_stage_desc<gpu_stage::shadow_cascade_1>(),
    gpu_stage_desc<gpu_stage::shadow_cascade_2>(),
    gpu_stage_desc<gpu_stage::shadow_cascade_3>(),
    gpu_stage_desc<gpu_stage::shadow_cascade_4>(),
    gpu_stage_desc<gpu_stage::world_pass>(),
    gpu_stage_desc<gpu_stage::world_geometry>(),
    gpu_stage_desc<gpu_stage::world_debug>(),
    gpu_stage_desc<gpu_stage::world_imgui>(),
};

}  // namespace

frame_recorder::frame_recorder(
    uint32 capacity
) {
    samples_.reserve(capacity);
    scratch_.reserve(capacity);
}

auto frame_recorder::record(const frame_sample& sample) -> void {
    samples_.push_back(sample);
}

auto frame_recorder::sample_count() const -> uint32 {
    return static_cast<uint32>(samples_.size());
}

auto frame_recorder::percentile_of(stage_getter get, float32 quantile) const -> float32 {
    if (samples_.empty()) {
        return 0.0f;
    }

    scratch_.clear();
    for (const auto& sample : samples_) {
        scratch_.push_back(get(sample));
    }
    std::ranges::sort(scratch_);

    const auto count = static_cast<float32>(scratch_.size());
    const auto rank  = static_cast<uint64>(std::ceil(quantile * count));
    const auto index = std::clamp<uint64>(rank, 1, scratch_.size()) - 1;
    return scratch_[index];
}

auto frame_recorder::report() const -> std::string {
    std::string out;
    auto sink = std::back_inserter(out);

    if (samples_.empty()) {
        return "no samples recorded\n";
    }

    std::format_to(sink, "samples: {}\n", samples_.size());

    const auto frame_p50 = percentile_of(cpu_stages.front().get, 0.50f);
    if (frame_p50 > 0.0f) {
        std::format_to(sink, "fps at median frame: {:.1f}\n", 1000.0f / frame_p50);
    }

    const auto write_row = [&](const stage_desc& stage) -> void {
        std::format_to(
            sink,
            "{:<21}{:9.3f}{:9.3f}{:9.3f}{:9.3f}\n",
            stage.name,
            percentile_of(stage.get, 0.50f),
            percentile_of(stage.get, 0.95f),
            percentile_of(stage.get, 0.99f),
            percentile_of(stage.get, 1.00f)
        );
    };

    std::format_to(
        sink, "\n{:<21}{:>9}{:>9}{:>9}{:>9}\n", "stage (ms)", "p50", "p95", "p99", "max"
    );

    for (const auto& stage : cpu_stages) {
        write_row(stage);
    }

    std::format_to(
        sink, "\n{:<21}{:>9}{:>9}{:>9}{:>9}\n", "system (ms)", "p50", "p95", "p99", "max"
    );
    for (const auto& stage : system_stages) {
        write_row(stage);
    }

    if (!samples_.front().render.gpu.supported) {
        std::format_to(sink, "\ngpu timestamps: unsupported on this device\n");
        return out;
    }

    std::format_to(sink, "\n");
    for (const auto& stage : gpu_stages) {
        write_row(stage);
    }

    return out;
}


auto frame_recorder::collect(gfx::report& out) const -> void {
    if (samples_.empty()) {
        return;
    }

    auto& section = out.section("stages");

    const auto write_stage = [&](const stage_desc& stage) -> void {
        auto& row = section.sub(std::string{stage.name});
        row.value("p50", static_cast<float64>(percentile_of(stage.get, 0.50f)))
            .value("p95", static_cast<float64>(percentile_of(stage.get, 0.95f)))
            .value("p99", static_cast<float64>(percentile_of(stage.get, 0.99f)))
            .value("max", static_cast<float64>(percentile_of(stage.get, 1.00f)));
    };

    for (const auto& stage : cpu_stages) {
        write_stage(stage);
    }

    {
        auto& systems = out.section("systems");
        for (const auto& stage : system_stages) {
            auto& row = systems.sub(std::string{stage.name});
            row.value("p50", static_cast<float64>(percentile_of(stage.get, 0.50f)))
                .value("p95", static_cast<float64>(percentile_of(stage.get, 0.95f)))
                .value("p99", static_cast<float64>(percentile_of(stage.get, 0.99f)))
                .value("max", static_cast<float64>(percentile_of(stage.get, 1.00f)));
        }
    }

    if (!samples_.front().render.gpu.supported) {
        return;
    }

    for (const auto& stage : gpu_stages) {
        write_stage(stage);
    }
}

}  // namespace vw::gfx
