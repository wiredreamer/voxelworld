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
    file_service& file_svc, clipboard_service& clipboard_svc
)
    : engine_(&eng)
    , state_(&state)
    , op_manager_(&op_manager)
    , file_service_(&file_svc)
    , clipboard_service_(&clipboard_svc) {}

auto menu_bar::render_edit_menu_() const -> void {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !op_manager_->is_undo_empty())) {
        op_manager_->undo();
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !op_manager_->is_redo_empty())) {
        op_manager_->redo();
    }

    ImGui::Separator();

    const auto item = [this](const char* label, command cmd) -> bool {
        const auto keys = std::string{keys_of(cmd)};
        return ImGui::MenuItem(label, keys.c_str(), false, is_available(cmd, *state_));
    };

    if (item("Cut", command::cut)) {
        clipboard_service_->cut();
    }
    if (item("Copy", command::copy)) {
        clipboard_service_->copy();
    }
    if (item("Paste", command::paste)) {
        clipboard_service_->begin_paste();
    }
    if (item("Erase Selection", command::erase_selection)) {
        clipboard_service_->erase();
    }

    ImGui::Separator();

    if (item("Select All", command::select_all)) {
        clipboard_service_->select_all();
    }
    const bool has_selection =
        state_->ctx.allows_volume_edit() && state_->volume.selection.has_value();
    const auto deselect_keys = std::string{keys_of(command::cancel)};
    if (ImGui::MenuItem("Deselect", deselect_keys.c_str(), false, has_selection)) {
        clipboard_service_->deselect();
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Volume", state_->ctx.allows_volume_edit())) {
        render_volume_menu_();
        ImGui::EndMenu();
    }
}

auto menu_bar::render_volume_menu_() const -> void {
    struct axis_item {
        asset::voxel_axis axis;
        const char* flip;
        const char* turn_forward;
        const char* turn_back;
    };

    constexpr std::array axis_items{
        axis_item{asset::voxel_axis::x, "Flip X", "Rotate X +90", "Rotate X -90"},
        axis_item{asset::voxel_axis::y, "Flip Y", "Rotate Y +90", "Rotate Y -90"},
        axis_item{asset::voxel_axis::z, "Flip Z", "Rotate Z +90", "Rotate Z -90"},
    };

    const auto& node_name = state_->edited_node();

    const auto it = state_->scene.name_to_entity.find(node_name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    const auto& world = engine_->get_world();
    if (!world.has<ecs::model_component>(it->second) ||
        !world.get<ecs::model_component>(it->second).has_model()) {
        return;
    }

    std::optional<asset::voxel_orientation> how;

    for (const auto& entry : axis_items) {
        if (ImGui::MenuItem(entry.flip)) {
            how = asset::mirrored_orientation(entry.axis);
        }
    }

    ImGui::Separator();

    for (const auto& entry : axis_items) {
        if (ImGui::MenuItem(entry.turn_forward)) {
            how = asset::rotated_orientation(entry.axis, 1);
        }
        if (ImGui::MenuItem(entry.turn_back)) {
            how = asset::rotated_orientation(entry.axis, -1);
        }
    }

    ImGui::Separator();

    const auto& occupied = state_->volume.occupied;
    const bool can_trim  = occupied.has_value() &&
                          occupied->size() != world.get<ecs::model_component>(it->second).size();
    const bool trims     = ImGui::MenuItem("Trim", nullptr, false, can_trim);

    if (how) {
        op_manager_->execute(
            std::make_unique<reorient_model_operation>(
                *engine_, *state_, reorient_model_params{.name = node_name, .how = *how}
            )
        );
    } else if (trims) {
        op_manager_->execute(
            std::make_unique<trim_model_operation>(
                *engine_, *state_, trim_model_params{.name = node_name}
            )
        );
    }
}

auto menu_bar::render_mcp_status_() const -> void {
    const mcp_status& mcp = state_->mcp;
    if (!mcp.enabled) {
        return;
    }

    const std::string label = mcp.listening
        ? std::format("MCP :{} | {}", mcp.port, mcp.requests)
        : std::string{"MCP off"};

    constexpr float32 right_margin = 12.0f;
    const float32 label_width      = ImGui::CalcTextSize(label.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - label_width - right_margin);

    if (mcp.listening) {
        ImGui::TextUnformatted(label.c_str());
    } else {
        ImGui::TextDisabled("%s", label.c_str());
    }

    if (ImGui::IsItemHovered()) {
        const std::string hint = mcp.listening
            ? std::format(
                  "MCP server on 127.0.0.1:{}, {} requests, last tool: {}", mcp.port, mcp.requests,
                  mcp.last_tool.empty() ? std::string_view{"none"} : std::string_view{mcp.last_tool}
              )
            : std::format("MCP server is not listening: {}", mcp.failure);
        ImGui::SetTooltip("%s", hint.c_str());
    }
}

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

    const bool placing_paste = state_->ctx.in_paste();

    ImGui::BeginDisabled(placing_paste);

    const bool has_prefab = !state_->file.filename.empty();
    if (ImGui::BeginMenu("Prefab")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            state_->ui.need_new_file_modal = true;
        }
        if (ImGui::MenuItem("Open", "Ctrl+O")) {
            state_->ui.need_open_file_modal = true;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, has_prefab)) {
            file_service_->save();
        }
        if (ImGui::MenuItem("Save As", "Ctrl+Shift+S", false, has_prefab)) {
            state_->ui.need_save_as_modal = true;
        }
        if (ImGui::MenuItem("Close", nullptr, false, has_prefab)) {
            state_->ui.need_close_file = true;
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
        render_edit_menu_();
        ImGui::EndMenu();
    }

    ImGui::EndDisabled();

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

    ImGui::BeginDisabled(placing_paste);

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

    ImGui::EndDisabled();

    state_->ui.left_offset += 20.0f;
    state_->ui.right_offset += 20.0f;

    render_mcp_status_();

    ImGui::EndMenuBar();

    ImGui::End();
}

}  // namespace vw::sculptor
