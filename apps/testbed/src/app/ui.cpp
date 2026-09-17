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

    ImGui::Begin("World Grid Test", nullptr, window_flags);
    ImGui::Text("Controls:");
    ImGui::Text("WASD + Mouse - moving");
    ImGui::Text("F1 - toggle cursor");
    ImGui::Text("LMB - use the tool, cursor captured");
    ImGui::Text("N - pause the sun, [ ] - move it");
    ImGui::Text("CTRL+F12 - engine debug tool");
    ImGui::Text("ESC - exit");
    ImGui::Separator();

    {
        const auto hour = static_cast<int32>(time_of_day_ * 24.0f);
        const auto minute =
            static_cast<int32>(((time_of_day_ * 24.0f) - static_cast<float32>(hour)) * 60.0f);
        ImGui::Text("%02d:%02d %s", hour, minute, day_night_running_ ? "" : "(paused)");

        float32 time = time_of_day_;
        if (ImGui::SliderFloat("Time", &time, 0.0f, 1.0f, "%.3f")) {
            time_of_day_ = time;
            apply_time_of_day_();
        }
        ImGui::SliderFloat("Day (s)", &day_length_seconds_, 8.0f, 600.0f, "%.0f");
    }

    ImGui::Separator();

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

        if (scene_ != nullptr) {
            scene_->ui();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Emitters")) {
        bool torch = torch_.is_valid();
        if (ImGui::Checkbox("Carry a torch", &torch)) {
            set_torch_(torch);
        }

        if (world_grid_ != nullptr) {
            if (ImGui::Button("Drop lamp")) {
                drop_emitter(voxels::world::glowstone, 1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Pour lava")) {
                drop_emitter(voxels::world::lava, 3);
            }

            if (!drop_status_.empty()) {
                ImGui::TextUnformatted(drop_status_.c_str());
            }
        }
    }

    ImGui::Separator();
    float speed = camera_controller_->get_camera_speed();
    if (ImGui::SliderFloat("Speed", &speed, 1.0f, 5000.0f, "%.0f")) {
        camera_controller_->set_camera_speed(speed);
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
            day_night_running_ = !day_night_running_;
            break;
        case plat::keyboard::keys::LEFT_BRACKET:
            step_time_of_day_(-0.02f);
            break;
        case plat::keyboard::keys::RIGHT_BRACKET:
            step_time_of_day_(0.02f);
            break;
        default:
            break;
    }
}

}  // namespace vw::testbed
