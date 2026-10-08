module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

auto testbed_app::render_ui() -> void {
    ImGuiIO& io = ImGui::GetIO();
    if (camera_controller_->is_mouse_captured()) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 window_pos             = ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + 10);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("Testbed", nullptr, window_flags);

    if (scene_ != nullptr) {
        ImGui::Text("scene: %s", std::string{scene_->name()}.c_str());
        scene_->ui();
    }

    float speed = camera_controller_->get_camera_speed();
    if (ImGui::SliderFloat("Speed", &speed, 1.0f, 5000.0f, "%.0f")) {
        camera_controller_->set_camera_speed(speed);
    }

    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::TextUnformatted("WASD + mouse - move");
        ImGui::TextUnformatted("F1 - toggle cursor");
        ImGui::TextUnformatted("LMB - use the tool, cursor captured");
        ImGui::TextUnformatted("N - pause the sun, [ ] - move it");
        ImGui::TextUnformatted("CTRL+F12 - engine debug tool");
        ImGui::TextUnformatted("ESC - exit");
    }

    if (ImGui::CollapsingHeader("Sky")) {
        day_night_.draw_controls(get_engine().get_renderer());
    }

    if (ImGui::CollapsingHeader("Grass")) {
        auto& grass       = get_engine().get_renderer().get_grass_settings();
        const auto& stats = get_engine().get_renderer().get_grass_stats();
        ImGui::Checkbox("Draw grass", &grass.enabled);
        ImGui::SliderInt("Radius, columns", &grass.radius_columns, 1, 6);
        ImGui::SliderFloat("Fade from", &grass.fade_share, 0.0f, 0.99f, "%.2f");
        auto& wind = get_engine().get_renderer().get_wind_settings();
        ImGui::SliderFloat2("Wind direction", &wind.direction.x, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wind speed", &wind.speed, 0.0f, 6.0f, "%.1f");
        ImGui::SliderFloat("Grass bend", &wind.grass_bend, 0.0f, 8.0f, "%.1f");
        ImGui::SliderFloat("Leaf sway, voxels", &wind.leaf_sway_voxels, 0.0f, 0.5f, "%.2f");
        ImGui::Text(
            "%u tufts, %u meshes, %u draws, %u chunks", stats.instances, stats.meshes, stats.draws,
            stats.chunks
        );
    }

    if (ImGui::CollapsingHeader("Editing")) {
        static constexpr std::array<const char*, 3> tool_names{"none", "place", "remove"};

        auto tool = static_cast<int32>(tool_);
        if (ImGui::Combo(
                "Tool", &tool, tool_names.data(), static_cast<int32>(tool_names.size())
            )) {
            tool_ = static_cast<edit_tool>(tool);
        }

        if (tool_ == edit_tool::place) {
            std::array<const char*, voxel_menu.size()> names{};
            for (std::size_t i = 0; i < voxel_menu.size(); ++i) {
                names[i] = voxel_menu[i].name;
            }
            ImGui::Combo("Voxel", &place_choice_, names.data(), static_cast<int32>(names.size()));
        }

        ImGui::SliderInt("Reach (voxels)", &reach_voxels_, 2, 32);

        if (tool_ == edit_tool::none) {
            ImGui::TextUnformatted("pick a tool, capture the cursor with F1, left click");
        } else if (!camera_controller_->is_mouse_captured()) {
            ImGui::TextUnformatted("F1 to capture the cursor");
        } else if (hovered_) {
            const vec3i& solid = hovered_->solid_voxel_pos;
            ImGui::Text("voxel %d,%d,%d", solid.x, solid.y, solid.z);
        } else {
            ImGui::TextUnformatted("nothing in reach");
        }

        ImGui::Text("edits: %d", edit_clicks_);
    }

    if (ImGui::CollapsingHeader("Emitters")) {
        bool torch = torch_.is_valid();
        if (ImGui::Checkbox("Carry a torch", &torch)) {
            set_torch_(torch);
        }

        if (world_grid_ != nullptr) {
            if (ImGui::Button("Drop lamp")) {
                drop_emitter(matter{voxels::amber[8], materials::lamp}, 1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Pour lava")) {
                drop_emitter(matter{voxels::red[8], materials::fire}, 3);
            }

            if (!drop_status_.empty()) {
                ImGui::TextUnformatted(drop_status_.c_str());
            }
        }
    }

    ImGui::End();
}

auto testbed_app::handle_key_press(
    plat::keyboard::keys key
) -> void {
    switch (key) {
        case plat::keyboard::keys::ESCAPE:
            get_engine().shutdown();
            break;
        case plat::keyboard::keys::F1:
            camera_controller_->toggle_mouse_captured();
            camera_controller_->toggle_keyboard_control_enabled();
            break;
        case plat::keyboard::keys::N:
            day_night_.set_running(!day_night_.is_running());
            break;
        case plat::keyboard::keys::LEFT_BRACKET:
            day_night_.step(-0.02f, get_engine().get_renderer());
            break;
        case plat::keyboard::keys::RIGHT_BRACKET:
            day_night_.step(0.02f, get_engine().get_renderer());
            break;
        default:
            break;
    }
}

}  // namespace vw::testbed
