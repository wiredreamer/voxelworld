module;

#include <imgui.h>

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shellapi.h>
#endif

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

menu_bar::menu_bar(
    engine_type& eng, app_state& state, operation_manager& op_manager,
    file_service& file_svc
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager), file_service_(&file_svc) {}

auto menu_bar::render(
    float
) const -> void {
    constexpr ImGuiWindowFlags menu_window_flags =  //
        ImGuiWindowFlags_MenuBar |                  //
        ImGuiWindowFlags_NoTitleBar |               //
        ImGuiWindowFlags_NoCollapse |               //
        ImGuiWindowFlags_NoResize |                 //
        ImGuiWindowFlags_NoMove |                   //
        ImGuiWindowFlags_NoBringToFrontOnFocus |    //
        ImGuiWindowFlags_NoNavFocus |               //
        ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2{viewport->WorkSize.x, 10.f});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MenuWindow", nullptr, menu_window_flags);
    ImGui::PopStyleVar(3);

    ImGui::BeginMenuBar();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New File", "Ctrl+N")) {
            state_->ui.need_new_file_modal = true;
        }
        if (ImGui::MenuItem("Open File", "Ctrl+O")) {
            state_->ui.need_open_file_modal = true;
        }
        if (ImGui::MenuItem("Save File", "Ctrl+S")) {
            file_service_->save();
        }
        if (ImGui::MenuItem("Save As", "Ctrl+Shift+S")) {
            state_->ui.need_save_as_modal = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Open Assets Folder")) {
            namespace fs = std::filesystem;
            const std::string assets_dir =
                fs::absolute(fs::path{app_state::asset_root_name}).string();
#ifdef _WIN32
            ShellExecuteA(nullptr, "open", assets_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elifdef __APPLE__
            static_cast<void>(std::system(("open \"" + assets_dir + "\"").c_str()));
#else
            static_cast<void>(std::system(("xdg-open \"" + assets_dir + "\"").c_str()));
#endif
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            engine_->shutdown();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !op_manager_->is_undo_empty())) {
            op_manager_->undo();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !op_manager_->is_redo_empty())) {
            op_manager_->redo();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Sockets", "Alt+S", state_->ui.show_sockets)) {
            state_->ui.show_sockets ^= true;
        }
        if (ImGui::MenuItem("Animation Timeline", "Alt+T", state_->ui.show_timeline)) {
            state_->ui.show_timeline ^= true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts")) {
            state_->ui.need_shortcuts_modal = true;
        }
        ImGui::EndMenu();
    }

    const bool has_clip = !state_->anim.selected_clip_name.empty();
    if (ImGui::BeginMenu("Animation")) {
        if (ImGui::MenuItem("New Clip")) {
            state_->ui.need_create_clip_modal = true;
        }
        if (ImGui::MenuItem("Open Clip")) {
            state_->ui.need_load_clip_modal = true;
        }
        if (ImGui::MenuItem("Animate", "Alt+A", false, has_clip && !state_->ctx.in_clip())) {
            state_->ui.need_enter_animation = true;
        }
        if (ImGui::MenuItem("Save Clip", nullptr, false, has_clip)) {
            state_->ui.need_save_clip = true;
        }
        if (ImGui::MenuItem("Close Clip", nullptr, false, has_clip)) {
            state_->ui.need_close_clip = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Play/Pause", "Space", false, has_clip)) {
            state_->anim.need_toggle_playback = true;
        }
        if (ImGui::MenuItem("Stop", nullptr, false, state_->ctx.in_clip())) {
            state_->anim.need_stop_playback = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Key Current Channel", "K", false, state_->ctx.in_clip())) {
            state_->anim.need_record_key = true;
        }
        if (ImGui::MenuItem("Key All Channels", "Shift+K", false, state_->ctx.in_clip())) {
            state_->anim.need_record_key_all = true;
        }
        if (ImGui::MenuItem(
                "Delete Keyframe",
                nullptr,
                false,
                state_->anim.selected_keyframe_id != asset::invalid_keyframe_id
            )) {
            state_->anim.need_delete_keyframe = true;
        }
        ImGui::EndMenu();
    }

    state_->ui.left_offset += 20.0f;
    state_->ui.right_offset += 20.0f;

    ImGui::EndMenuBar();

    ImGui::End();
}

}  // namespace vw::sculptor
