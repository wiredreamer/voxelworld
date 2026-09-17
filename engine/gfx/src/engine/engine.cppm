export module vw.gfx:engine;

export import :engine.app;
export import :engine.stats;
export import :engine.report;
import :debug.window;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :camera;
import :render;
import :renderer;
import :engine.frame_recorder;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {

// Владелец окна, контекста Vulkan, рендерера и мира; крутит цикл кадра и раздаёт
// приложению всё, до чего оно может дотянуться.
class engine final {
public:
    using renderer_type     = renderer;
    using world_type        = world;
    using debug_window_type = debug_window;
    using app_type          = app;

    engine(int32 width, int32 height, std::string_view title, bench_config bench = {});
    ~engine();

    engine(const engine&)                    = delete;
    auto operator=(const engine&) -> engine& = delete;

    template <typename TApp, typename... TArgs>
    auto run(TArgs&&... args) -> void {
        app_ = std::make_unique<TApp>(*this, std::forward<TArgs>(args)...);
        main_loop();
    }

    auto shutdown() -> void;

    [[nodiscard]] auto get_window() const -> window&;
    [[nodiscard]] auto get_vulkan_context() const -> vulkan_context&;
    [[nodiscard]] auto get_renderer() const -> renderer_type&;
    [[nodiscard]] auto get_camera() const -> camera&;
    [[nodiscard]] auto get_world() const -> world_type&;
    [[nodiscard]] auto get_voxel_registry() const -> const voxel_registry&;
    [[nodiscard]] auto get_debug_tool() const -> debug_window_type&;

    // Ноль означает, что загрузчик выберет своё умолчание.
    [[nodiscard]] auto get_terrain_workers() const -> uint32 {
        return bench_.terrain_workers;
    }

    [[nodiscard]] auto get_stats() const -> const engine_stats&;

private:
    auto main_loop() -> void;

    auto render(float32 delta_time) -> void;

    auto bench_tick_() -> void;
    auto write_bench_report_() const -> void;

    auto update_stats() -> void;
    [[nodiscard]] static auto calculate_ram_usage() -> uint64;
    [[nodiscard]] static auto calculate_commit_usage() -> uint64;
    [[nodiscard]] auto calculate_vram_usage() const -> uint64;

    std::unique_ptr<window> window_;
    std::unique_ptr<vulkan_context> vulkan_context_;
    std::unique_ptr<renderer_type> renderer_;
    std::unique_ptr<camera> camera_;
    voxel_registry voxel_registry_;
    std::unique_ptr<world_type> world_;
    std::unique_ptr<debug_window_type> debug_tool_;

    std::unique_ptr<app_type> app_;

    bool running_ = false;
    std::chrono::high_resolution_clock::time_point start_time_ =
        std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point last_frame_time_;

    mutable engine_stats stats_;
    std::chrono::high_resolution_clock::time_point frame_start_time_;
    std::chrono::high_resolution_clock::time_point last_memory_update_time_;

    bench_config bench_;
    std::unique_ptr<frame_recorder> recorder_;
    uint64 frame_index_       = 0;
    uint64 bench_start_frame_ = 0;

    // Сколько сцена стримилась целиком: всё сгенерировано, смешено и загружено.
    // Единственное число, которое двигают счётчики воркеров.
    float32 ready_ms_    = 0.0f;
    uint64 ready_frames_ = 0;
    bool ready_recorded_ = false;

    static constexpr float32 memory_update_interval_sec_ = 1.0f;

    event_sub<window_resize_event> window_resize_sub_;
    event_sub<key_press_event> key_press_sub_;
};

}  // namespace vw::gfx
