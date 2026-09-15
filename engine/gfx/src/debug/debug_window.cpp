module;

#include <imgui.h>

module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

debug_window::debug_window(
    engine_type& engine
)
    : engine_(&engine) {}

auto debug_window::render(
    [[maybe_unused]] float32 delta_time
) -> void {
    if (!visible_) {
        return;
    }

    render_main_window();
    render_panels();
    submit_debug_draws();
}

auto debug_window::toggle_visibility() -> void {
    visible_ = !visible_;
}

auto debug_window::set_visible(
    bool visible
) -> void {
    visible_ = visible;
}
auto debug_window::is_visible() const -> bool {
    return visible_;
}

auto debug_window::render_main_window() -> void {
    ImGuiWindowFlags window_flags =          //
        ImGuiWindowFlags_NoResize |          //
        ImGuiWindowFlags_NoScrollbar |       //
        ImGuiWindowFlags_NoCollapse |        //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoSavedSettings |   //
        ImGuiWindowFlags_MenuBar;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 window_pos =
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10, viewport->WorkPos.y + 10);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));

    if (ImGui::Begin("Debug Tool", &visible_, window_flags)) {
        render_menu_bar();

        const auto& eng_stats = engine_->get_stats();

        ImGui::Text("frame_ms  %.2fms fps %.0f", eng_stats.frame_ms, eng_stats.fps);
        ImGui::Text("update_ms %.2fms", eng_stats.world_update_ms);
        ImGui::Text("render_ms %.2fms", eng_stats.world_render_ms);

        ImGui::Text(
            "RAM %.2fMB VRAM %.2fMB",
            eng_stats.ram_usage_bytes / (1024.0f * 1024.0f),
            eng_stats.vram_usage_bytes / (1024.0f * 1024.0f)
        );

        ImGui::Text("draw calls %u", engine_->get_renderer().get_stats().draw_call_count);

        ImGui::Spacing();
        render_frame_plot();
    }
    ImGui::End();
}

auto debug_window::render_menu_bar() -> void {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("Stats")) {
        for (std::size_t i = 0; i < first_settings_panel; ++i) {
            ImGui::MenuItem(panel_names[i], nullptr, &panel_open_[i]);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        for (std::size_t i = first_settings_panel; i < panel_count; ++i) {
            ImGui::MenuItem(panel_names[i], nullptr, &panel_open_[i]);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

auto debug_window::render_frame_plot() -> void {
    const float32 frame_ms  = engine_->get_stats().frame_ms;
    fps_bucket_current_max_ = std::max(fps_bucket_current_max_, frame_ms);
    fps_bucket_current_min_ = std::min(fps_bucket_current_min_, frame_ms);
    fps_bucket_accum_ms_   += frame_ms;
    if (fps_bucket_accum_ms_ >= fps_bucket_duration_ms_) {
        fps_bucket_max_[fps_bucket_index_] = fps_bucket_current_max_;
        fps_bucket_min_[fps_bucket_index_] = fps_bucket_current_min_;
        fps_bucket_index_                  = (fps_bucket_index_ + 1) % fps_bucket_count_;
        if (fps_bucket_filled_count_ < fps_bucket_count_) {
            ++fps_bucket_filled_count_;
        }
        fps_bucket_accum_ms_    = 0.0f;
        fps_bucket_current_max_ = 0.0f;
        fps_bucket_current_min_ = std::numeric_limits<float32>::max();
    }

    float32 window_min = std::numeric_limits<float32>::max();
    float32 window_max = 0.0f;
    for (std::size_t i = 0; i < fps_bucket_filled_count_; ++i) {
        window_min = std::min(window_min, fps_bucket_min_[i]);
        window_max = std::max(window_max, fps_bucket_max_[i]);
    }
    if (fps_bucket_filled_count_ == 0) {
        window_min = 0.0f;
    }

    std::array<char, 96> overlay{};
    std::snprintf(
        overlay.data(), overlay.size(), "peaks 10s  min %.2f max %.2f ms",
        static_cast<float64>(window_min), static_cast<float64>(window_max)
    );

    const auto plot_count  = static_cast<int32>(fps_bucket_filled_count_);
    const auto plot_offset = (fps_bucket_filled_count_ < fps_bucket_count_)
        ? 0
        : static_cast<int32>(fps_bucket_index_);

    ImGui::PlotLines(
        "##frame_ms_peaks",
        fps_bucket_max_.data(),
        plot_count,
        plot_offset,
        overlay.data(),
        0.0f,
        std::numeric_limits<float32>::max(),
        ImVec2(280.0f, 50.0f)
    );
}

auto debug_window::render_panels() -> void {
    ImGuiWindowFlags window_flags =          //
        ImGuiWindowFlags_NoCollapse |        //
        ImGuiWindowFlags_NoSavedSettings |   //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoFocusOnAppearing;

    for (std::size_t i = 0; i < panel_count; ++i) {
        if (!panel_open_[i]) {
            continue;
        }

        // Открытость держит сам ImGui через крестик окна, поэтому флаг уходит
        // туда ссылкой, а тело рисуется, только пока окно развёрнуто.
        if (ImGui::Begin(panel_titles[i], &panel_open_[i], window_flags)) {
            render_panel_body(static_cast<panel>(i));
        }
        ImGui::End();
    }
}

auto debug_window::render_panel_body(
    panel id
) -> void {
    switch (id) {
        case panel::systems:
            render_systems_panel();
            break;
        case panel::render:
            render_render_panel();
            break;
        case panel::buffers:
            render_buffers_panel();
            break;
        case panel::world:
            render_world_panel();
            break;
        case panel::animation:
            render_animation_panel();
            break;
        case panel::view:
            render_view_panel();
            break;
        case panel::lighting:
            render_lighting_panel();
            break;
        case panel::shadows:
            render_shadows_panel();
            break;
        case panel::lights:
            render_lights_panel();
            break;
        case panel::fog:
            render_fog_panel();
            break;
    }
}

auto debug_window::submit_debug_draws() -> void {
    if (show_colliders_) {
        engine_->get_renderer().draw_colliders(engine_->get_world());
    }
}

auto debug_window::metric_row(
    const char* name, float32 ms
) -> void {
    auto& max_val = metric_max_[name];
    if (ms > max_val) {
        max_val = ms;
    }

    ImVec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    if (ms > 5.0f) {
        color = {1.0f, 0.3f, 0.3f, 1.0f};
    } else if (ms > 1.0f) {
        color = {1.0f, 0.8f, 0.2f, 1.0f};
    }
    ImGui::TextColored(color, "%-14s %6.2f ms  max %5.2f ms", name, ms, max_val);
}

auto debug_window::count_row(
    const char* name, uint32 value
) const -> void {
    ImGui::Text("%-18s %8u", name, value);
}

}  // namespace vw::gfx
