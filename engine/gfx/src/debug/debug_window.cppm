export module vw.gfx:debug.window;

import std;

import vw.core;
import vw.ecs;

export namespace vw::gfx {

class engine;

class debug_window final {
public:
    using engine_type = engine;

    explicit debug_window(engine_type& engine);
    ~debug_window() = default;

    debug_window(const debug_window&)                    = delete;
    auto operator=(const debug_window&) -> debug_window& = delete;

    debug_window(debug_window&&)                    = default;
    auto operator=(debug_window&&) -> debug_window& = default;

    auto render(float32 delta_time) -> void;

    auto toggle_visibility() -> void;
    auto set_visible(bool visible) -> void;

    [[nodiscard]] auto is_visible() const -> bool;

private:
    enum class panel : uint8 {
        systems,
        render,
        buffers,
        world,
        animation,
        view,
        lighting,
        shadows,
        lights,
        fog,
        lod,
    };

    static constexpr std::size_t panel_count = static_cast<std::size_t>(panel::lod) + 1;
    static constexpr std::size_t first_settings_panel =
        static_cast<std::size_t>(panel::view);

    static constexpr std::array<const char*, panel_count> panel_names{
        "Systems", "Render",   "Buffers", "World",  "Animation",
        "View",    "Lighting", "Shadows", "Lights", "Fog",
        "LOD",
    };

    static constexpr std::array<const char*, panel_count> panel_titles{
        "Debug Tool - Systems",   "Debug Tool - Render",
        "Debug Tool - Buffers",   "Debug Tool - World",
        "Debug Tool - Animation", "Debug Tool - View",
        "Debug Tool - Lighting",  "Debug Tool - Shadows",
        "Debug Tool - Lights",    "Debug Tool - Fog",
        "Debug Tool - LOD",
    };

    auto render_main_window() -> void;
    auto render_menu_bar() -> void;
    auto render_frame_plot() -> void;
    auto render_panels() -> void;
    auto render_panel_body(panel id) -> void;

    auto submit_debug_draws() -> void;

    auto metric_row(const char* name, float32 ms) -> void;
    auto count_row(const char* name, uint32 value) const -> void;

    auto render_systems_panel() -> void;
    auto render_render_panel() -> void;
    auto render_buffers_panel() -> void;
    auto render_world_panel() -> void;
    auto render_animation_panel() -> void;
    auto render_animation_event_log_() -> void;
    auto collect_animation_events_(float32 delta_time) -> void;

    auto render_view_panel() -> void;
    auto render_lighting_panel() -> void;
    auto render_shadows_panel() -> void;
    auto render_lights_panel() -> void;
    auto render_fog_panel() -> void;
    auto render_lod_panel() -> void;

    engine_type* engine_;

    bool visible_ = false;
    std::array<bool, panel_count> panel_open_{};

    bool show_colliders_ = false;

    ecs::entity animation_entity_;

    struct logged_animation_event {
        ecs::entity entity;
        float64 seconds = 0.0;
        std::size_t layer = 0;
        std::string clip;
        std::string name;
        std::string payload;
    };

    static constexpr std::size_t animation_event_log_capacity_ = 64;
    static constexpr float64 fresh_event_seconds_              = 0.5;

    std::deque<logged_animation_event> animation_event_log_;
    float64 debug_clock_seconds_ = 0.0;

    std::unordered_map<std::string, float32> metric_max_;

    static constexpr uint64 fps_bucket_count_        = 200;
    static constexpr float32 fps_bucket_duration_ms_ = 50.0f;

    std::array<float32, fps_bucket_count_> fps_bucket_max_{};
    std::array<float32, fps_bucket_count_> fps_bucket_min_{};
    uint64 fps_bucket_index_        = 0;
    uint64 fps_bucket_filled_count_ = 0;
    float32 fps_bucket_accum_ms_    = 0.0f;
    float32 fps_bucket_current_max_ = 0.0f;
    float32 fps_bucket_current_min_ = std::numeric_limits<float32>::max();
};

}  // namespace vw::gfx
