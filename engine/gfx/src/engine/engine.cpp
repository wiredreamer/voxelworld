module;

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;
import :engine;
import :engine.frame_recorder;

namespace vw::gfx {

namespace {
constexpr log::log_category lc_bench_{"bench"};

constexpr std::string_view build_config =
#ifdef NDEBUG
    "Release";
#else
    "Debug";
#endif
}  // namespace

engine::engine(
    int32 width, int32 height, std::string_view title, bench_config bench
)
    : bench_(std::move(bench)) {
    if (bench_.enabled()) {
        recorder_ = std::make_unique<frame_recorder>(bench_.measure_frames);
    }

    jobs_           = std::make_unique<vw::job_system>(bench_.workers);
    window_         = std::make_unique<window>(width, height, title);
    vulkan_context_ = std::make_unique<vulkan_context>(*window_);
    renderer_       = std::make_unique<renderer_type>(
        *vulkan_context_, *window_, voxel_registry_, *jobs_);
    camera_ =
        std::make_unique<camera>(45.0f, static_cast<float>(width) / static_cast<float>(height));
    world_      = std::make_unique<world_type>(voxel_registry_);
    debug_tool_ = std::make_unique<debug_window_type>(*this);

    app_ = std::make_unique<app_type>(*this);

    window_resize_sub_ =
        window_->sub<window_resize_event>([this](const window_resize_event& event) -> bool {
            if (event.width > 0 && event.height > 0) {
                const float aspect =
                    static_cast<float>(event.width) / static_cast<float>(event.height);
                camera_->set_aspect_ratio(aspect);
                renderer_->handle_resize();
            }
            return false;
        });

    key_press_sub_ = window_->sub<key_press_event>([this](const key_press_event& event) -> bool {
        using keys = keyboard::keys;
        using mods = keyboard::mods;

        if (event.key == keys::F12 && event.with(mods::CTRL)) {
            debug_tool_->toggle_visibility();
        }
        return false;
    });

    last_memory_update_time_ = std::chrono::high_resolution_clock::now();
}

engine::~engine() {
    shutdown();
}

auto engine::shutdown() -> void {
    running_ = false;
    renderer_->get_mesh_pool().stop_gen_threads();
    renderer_->wait_idle();
}

auto engine::get_window() const -> window& {
    return *window_;
}

auto engine::get_vulkan_context() const -> vulkan_context& {
    return *vulkan_context_;
}

auto engine::get_renderer() const -> renderer_type& {
    return *renderer_;
}

auto engine::get_camera() const -> camera& {
    return *camera_;
}

auto engine::get_world() const -> world_type& {
    return *world_;
}

auto engine::get_voxel_registry() const -> const voxel_registry& {
    return voxel_registry_;
}

auto engine::get_debug_tool() const -> debug_window_type& {
    return *debug_tool_;
}

auto engine::main_loop() -> void {
    running_         = true;
    last_frame_time_ = std::chrono::high_resolution_clock::now();

    while (running_ && !window_->should_close()) {
        frame_start_time_ = std::chrono::high_resolution_clock::now();

        auto current_time = std::chrono::high_resolution_clock::now();
        auto delta_time   = std::chrono::duration<float>(current_time - last_frame_time_).count();
        delta_time        = std::min(delta_time, 0.1f);

        if (bench_.fixed_delta_seconds > 0.0f) {
            delta_time = bench_.fixed_delta_seconds;
        }

        window_->poll_events();

        render(delta_time);

        update_stats();
        last_frame_time_ = current_time;
        ++frame_index_;

        if (bench_.enabled()) {
            bench_tick_();
        }
    }
}

auto engine::bench_tick_() -> void {
    if (!app_->is_bench_ready()) {
        bench_start_frame_ = frame_index_;
        return;
    }

    if (!ready_recorded_) {
        ready_recorded_ = true;
        ready_frames_   = frame_index_;
        ready_ms_       = std::chrono::duration<float32, std::milli>(
                        std::chrono::high_resolution_clock::now() - start_time_
                    ).count();
    }

    if (frame_index_ - bench_start_frame_ <= bench_.warmup_frames) {
        return;
    }

    recorder_->record({
        .engine  = stats_,
        .render  = renderer_->get_stats().timing,
        .grid    = world_->system<ecs::world_grid_system>().get_stats(),
        .systems = world_->get_update_stats(),
    });

    if (recorder_->sample_count() >= bench_.measure_frames) {
        write_bench_report_();
        shutdown();
    }
}

auto engine::write_bench_report_() const -> void {
    const auto props = vulkan_context_->get_physical_device().getProperties();

    std::string report_text = std::format(
        "gpu: {}\nbuild: {}\npresent mode: {}\nframes in flight: {}\nwarmup frames: {}\n{}",
        static_cast<const char*>(props.deviceName),
        build_config,
        renderer_->get_present_mode_name(),
        renderer_type::get_frames_in_flight(),
        bench_.warmup_frames,
        recorder_->report()
    );

    const auto meshing = renderer_->get_mesh_pool().get_gen_stats();
    std::format_to(
        std::back_inserter(report_text),
        "\nmeshing: {} chunks, {} quads, {:.1f} ms total\n"
        "  per chunk (us): mean {:.0f}  p50 {:.0f}  p99 {:.0f}  max {:.0f}\n"
        "  queue: {} left, {} peak; held {}, {} peak\n",
        meshing.chunks,
        meshing.quads,
        meshing.total_ms,
        meshing.mean_us,
        meshing.p50_us,
        meshing.p99_us,
        meshing.max_us,
        meshing.queue_depth,
        meshing.queue_peak,
        meshing.held,
        meshing.held_peak
    );

    const auto columns = world_->system<ecs::world_grid_system>().get_loader_stats();
    std::format_to(
        std::back_inserter(report_text),
        "\nterrain: {} columns, {} chunks, {:.1f} ms total\n"
        "  per column (us): mean {:.0f}  p50 {:.0f}  p99 {:.0f}  max {:.0f}\n"
        "  queue: {} left, {} peak\n",
        columns.columns,
        columns.chunks,
        columns.total_ms,
        columns.mean_us,
        columns.p50_us,
        columns.p99_us,
        columns.max_us,
        columns.queue_depth,
        columns.queue_peak
    );

    const auto light      = world_->system<ecs::world_grid_system>().get_light_stats();
    const auto grid_stats = world_->system<ecs::world_grid_system>().get_stats();
    std::format_to(
        std::back_inserter(report_text),
        "\nsky light: {} columns, rows {:.1f} ms, flood {:.1f} ms, bake {:.1f} ms\n"
        "  per column (us): mean {:.0f}  p50 {:.0f}  p99 {:.0f}  max {:.0f}\n"
        "  queue: {} left, {} peak\n"
        "  relight: {} columns after edits, {} chunks changed, {} in the backlog\n",
        light.columns,
        light.rows_ms,
        light.flood_ms,
        light.bake_ms,
        light.mean_us,
        light.p50_us,
        light.p99_us,
        light.max_us,
        light.queue_depth,
        light.queue_peak,
        grid_stats.relit_columns,
        grid_stats.relit_chunks,
        grid_stats.relight_backlog
    );

    std::format_to(
        std::back_inserter(report_text),
        "\nscene ready after {} frames, {:.0f} ms\n",
        ready_frames_,
        ready_ms_
    );

    const auto grid = world_->system<ecs::world_grid_system>().get_stats();
    std::format_to(
        std::back_inserter(report_text),
        "\ngrid: {} chunks loaded, {} drawn, {} buried in rock or open air\n",
        grid.loaded_count,
        grid.drawn_count,
        grid.loaded_count - grid.drawn_count
    );

    const auto& pool  = world_->resource<asset::model_registry>().get_page_pool();
    const auto in_use = pool.allocated_count();
    constexpr auto addressable = asset::page_pool::block_size * asset::page_pool::max_blocks;

    constexpr auto to_mb = 1.0F / (1024.0F * 1024.0F);

    std::format_to(
        std::back_inserter(report_text),
        "\npages: {} of {} in use ({:.1f}% of the addressable limit), {} free, {:.0f} MB\n"
        "memory: ram {:.0f} MB now, {:.0f} MB peak; commit {:.0f} MB now, {:.0f} MB peak; "
        "vram {:.0f} MB now, {:.0f} MB peak\n",
        in_use,
        addressable,
        100.0F * static_cast<float32>(in_use) / static_cast<float32>(addressable),
        pool.free_count(),
        static_cast<float32>(in_use + pool.free_count()) *
            static_cast<float32>(sizeof(asset::page_pool::page_type)) * to_mb,
        static_cast<float32>(stats_.ram_usage_bytes) * to_mb,
        static_cast<float32>(stats_.ram_peak_bytes) * to_mb,
        static_cast<float32>(stats_.commit_bytes) * to_mb,
        static_cast<float32>(stats_.commit_peak_bytes) * to_mb,
        static_cast<float32>(stats_.vram_usage_bytes) * to_mb,
        static_cast<float32>(stats_.vram_peak_bytes) * to_mb
    );

    const auto& registry = world_->registry();
    std::format_to(
        std::back_inserter(report_text),
        "ecs: {} pools, {:.1f} MB in pools\n",
        registry.pool_count(),
        static_cast<float32>(registry.memory_bytes()) * to_mb
    );

    const auto& buffers = renderer_->get_stats().combined_buffers;

    float32 slot_mb = 0.0F;
    float32 used_mb = 0.0F;

    report_text += "\nbuffers by size class (quads): slots, load, MB\n";

    for (const auto& b : buffers.buffers) {
        const auto slot_bytes = static_cast<float32>(b.chunk_size.quad_count) * sizeof(quad);

        const auto held = static_cast<float32>(b.mesh_capacity) * slot_bytes * to_mb;
        const auto full = static_cast<float32>(b.mesh_count) * slot_bytes * to_mb;

        slot_mb += held;
        used_mb += full * b.quad_load_avg;

        std::format_to(
            std::back_inserter(report_text),
            "  {:>8}  {:>5} of {:<6}  peak {:<6}  water {:<6}  load {:.2f}  {:.1f} MB\n",
            b.chunk_size.quad_count,
            b.mesh_count,
            b.mesh_capacity,
            b.mesh_peak,
            b.mesh_high_water,
            b.quad_load_avg,
            held
        );
    }

    std::format_to(
        std::back_inserter(report_text),
        "  total {:.0f} MB of slots, about {:.0f} MB of it geometry\n",
        slot_mb,
        used_mb
    );

    const auto& cull = renderer_->get_stats().combined_buffers.chunk_cull;
    std::format_to(
        std::back_inserter(report_text),
        "\nchunk cull: {} of {} chunks visible ({:.1f}% hidden), walk {:.3f} ms\n"
        "  walked {} cells, {} of them empty; {} chunks with links, {} sealed, "
        "{} merged, {} pockets at most\n",
        cull.visible,
        cull.chunks,
        cull.chunks > 0
            ? 100.0F * static_cast<float32>(cull.chunks - cull.visible) /
                  static_cast<float32>(cull.chunks)
            : 0.0F,
        cull.walk_ms,
        cull.visited,
        cull.visited_empty,
        cull.known_links,
        cull.sealed,
        cull.merged,
        cull.max_pockets
    );

    report extra;
    if (app_) {
        app_->collect_report(extra);
    }
    report_text += extra.to_text();

    log::info(lc_bench_, "benchmark report\n{}", report_text);

    if (!bench_.report_path.empty()) {
        std::ofstream out{bench_.report_path};
        if (!out) {
            log::error(lc_bench_, "cannot write bench report to {}", bench_.report_path);
        } else {
            out << report_text;
        }
    }

    if (bench_.json_path.empty()) {
        return;
    }

    report structured;
    structured.section("run")
        .value("gpu", std::string_view{static_cast<const char*>(props.deviceName)})
        .value("build", std::string_view{build_config})
        .value("present_mode", std::string_view{renderer_->get_present_mode_name()})
        .value("frames_in_flight", static_cast<uint64>(renderer_type::get_frames_in_flight()))
        .value("warmup_frames", static_cast<uint64>(bench_.warmup_frames))
        .value("measured_frames", static_cast<uint64>(recorder_->sample_count()))
        .value("ready_frames", static_cast<uint64>(ready_frames_))
        .value("ready_ms", static_cast<float64>(ready_ms_), 1);

    recorder_->collect(structured);

    structured.section("meshing")
        .value("chunks", meshing.chunks)
        .value("quads", meshing.quads)
        .value("total_ms", static_cast<float64>(meshing.total_ms), 1)
        .value("mean_us", static_cast<float64>(meshing.mean_us), 0)
        .value("p50_us", static_cast<float64>(meshing.p50_us), 0)
        .value("p99_us", static_cast<float64>(meshing.p99_us), 0)
        .value("max_us", static_cast<float64>(meshing.max_us), 0)
        .value("queue_peak", static_cast<uint64>(meshing.queue_peak));

    if (app_) {
        app_->collect_report(structured);
    }

    std::ofstream json{bench_.json_path};
    if (!json) {
        log::error(lc_bench_, "cannot write bench json to {}", bench_.json_path);
        return;
    }
    json << structured.to_json();
}

auto engine::render(
    float delta_time
) -> void {
    stats_.world_update_ms = measure_ms([&] { world_->update(delta_time); });

    stats_.world_render_ms = measure_ms([&] {
        bool opened = false;
        stats_.begin_frame_ms = measure_ms([&] { opened = renderer_->begin_frame(); });
        if (!opened) {
            return;
        }
        stats_.app_render_ms  = measure_ms([&] {
            app_->render(delta_time);
            debug_tool_->render(delta_time);
        });
        stats_.renderer_ms  = measure_ms([&] { renderer_->render(*world_, *camera_); });
        stats_.end_frame_ms = measure_ms([&] { renderer_->end_frame(); });
    });

    world_->clear_changed();
}

auto engine::get_stats() const -> const engine_stats& {
    return stats_;
}

auto engine::update_stats() -> void {
    const auto current_time = std::chrono::high_resolution_clock::now();
    stats_.frame_ms =
        std::chrono::duration<float32>(current_time - frame_start_time_).count() * 1000.0f;

    if (stats_.frame_ms > 0.0f) {
        stats_.fps = 1000.0f / stats_.frame_ms;
    }

    const auto time_since_last_memory_update =
        std::chrono::duration<float32>(current_time - last_memory_update_time_).count();
    if (time_since_last_memory_update >= memory_update_interval_sec_) {
        stats_.ram_usage_bytes   = calculate_ram_usage();
        stats_.commit_bytes      = calculate_commit_usage();
        stats_.vram_usage_bytes  = calculate_vram_usage();
        stats_.ram_peak_bytes    = std::max(stats_.ram_peak_bytes, stats_.ram_usage_bytes);
        stats_.commit_peak_bytes = std::max(stats_.commit_peak_bytes, stats_.commit_bytes);
        stats_.vram_peak_bytes   = std::max(stats_.vram_peak_bytes, stats_.vram_usage_bytes);
        last_memory_update_time_ = current_time;
    }
}

auto engine::calculate_ram_usage() -> uint64 {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)
        )) {
        return pmc.WorkingSetSize;
    }
#endif
    return 0;
}

auto engine::calculate_commit_usage() -> uint64 {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)
        )) {
        return pmc.PrivateUsage;
    }
#endif
    return 0;
}

auto engine::calculate_vram_usage() const -> uint64 {
    const auto chain = vulkan_context_->get_physical_device().getMemoryProperties2<
        vk::PhysicalDeviceMemoryProperties2,
        vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

    const auto& mem_props = chain.get<vk::PhysicalDeviceMemoryProperties2>().memoryProperties;
    const auto& budget    = chain.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

    uint64 total_usage = 0;
    for (uint32 i = 0; i < mem_props.memoryHeapCount; ++i) {
        if (mem_props.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
            total_usage += budget.heapUsage[i];
        }
    }
    return total_usage;
}

}  // namespace vw::gfx
