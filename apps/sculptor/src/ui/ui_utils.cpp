module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {
namespace {

constexpr float32 panel_margin = 10.f;

auto slot_offset(app_state& state, panel_slot slot) -> float32& {
    switch (slot) {
        case panel_slot::left: return state.ui.left_offset;
        case panel_slot::right: return state.ui.right_offset;
        case panel_slot::bottom:
        case panel_slot::footer: break;
    }
    return state.ui.bottom_offset;
}

}  // namespace

auto begin_panel(
    app_state& state, panel_slot slot, std::string_view title, bool* open, bool title_bar
) -> void {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float32 offset          = slot_offset(state, slot);

    ImVec2 pos;
    ImVec2 pivot;

    switch (slot) {
        case panel_slot::left:
            pos   = {viewport->WorkPos.x + panel_margin, viewport->WorkPos.y + offset + panel_margin};
            pivot = {0.f, 0.f};
            break;
        case panel_slot::right:
            pos = {
                viewport->WorkPos.x + viewport->WorkSize.x - panel_margin,
                viewport->WorkPos.y + offset + panel_margin
            };
            pivot = {1.f, 0.f};
            break;
        case panel_slot::bottom:
        case panel_slot::footer:
            pos = {
                viewport->WorkPos.x + panel_margin,
                viewport->WorkPos.y + viewport->WorkSize.y - offset - panel_margin
            };
            pivot = {0.f, 1.f};
            break;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);

    ImGuiWindowFlags flags =                //
        ImGuiWindowFlags_NoCollapse |       //
        ImGuiWindowFlags_NoSavedSettings |  //
        ImGuiWindowFlags_NoMove;

    if (!title_bar) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    if (slot == panel_slot::footer) {
        ImGui::SetNextWindowSize(
            ImVec2{
                viewport->WorkSize.x - panel_margin * 2.f,
                state.ui.bottom_panel_height - panel_margin
            },
            ImGuiCond_Always
        );
        flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }

    ImGui::Begin(title.data(), open, flags);
}

auto end_panel(
    app_state& state, panel_slot slot
) -> void {
    slot_offset(state, slot) += ImGui::GetWindowHeight() + panel_margin;
    ImGui::End();
}

auto imgui_input_text_string(
    std::string_view label, std::string& value, float32 label_column
) -> void {
    constexpr std::size_t max_length = 64;
    std::array<char, max_length> buffer{};

#ifdef _WIN32
    strncpy_s(buffer.data(), max_length, value.data(), max_length - 1);
#else
    std::strncpy(buffer.data(), value.data(), max_length - 1);
#endif

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_column);

    const auto hidden_label = std::format("##{}", label);
    if (ImGui::InputText(hidden_label.c_str(), buffer.data(), max_length)) {
        value = std::string{buffer.data()};
    }
}

auto imgui_input_int_left(
    std::string_view label, int* value
) -> bool {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine();

    const auto hidden_label = std::format("##{}", label);
    return ImGui::InputInt(hidden_label.c_str(), value);
}

auto imgui_clamp_window_pos_to_viewport() -> void {
    if (!ImGui::IsWindowCollapsed() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 window_pos       = ImGui::GetWindowPos();
        const ImVec2 window_size      = ImGui::GetWindowSize();

        const auto viewport_pos  = ImVec2{viewport->WorkPos.x + 10, viewport->WorkPos.y + 10};
        const auto viewport_size = ImVec2{viewport->WorkSize.x - 20, viewport->WorkSize.y - 20};

        const float new_x = std::max(
            viewport_pos.x, std::min(window_pos.x, viewport_pos.x + viewport_size.x - window_size.x)
        );
        const float new_y = std::max(
            viewport_pos.y, std::min(window_pos.y, viewport_pos.y + viewport_size.y - window_size.y)
        );

        if (new_x != window_pos.x || new_y != window_pos.y) {
            ImGui::SetWindowPos(ImVec2(new_x, new_y));
        }
    }
}

auto imgui_drag_vec3f(std::string_view label, vec3f& vec, float label_offset) -> drag_edit {
    drag_edit edit;

    const auto field_id = std::format("##drag_vec3f_{}", label);
    ImGui::PushID(field_id.c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_offset);

    ImGui::PushItemWidth(80.0f);
    edit.changed |= ImGui::DragFloat(std::format("##{}X", label).c_str(), &vec.x, 0.1f, 0, 0, "%.4f");
    edit.started |= ImGui::IsItemActivated();
    edit.finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushItemWidth(80.0f);
    edit.changed |= ImGui::DragFloat(std::format("##{}Y", label).c_str(), &vec.y, 0.1f, 0, 0, "%.4f");
    edit.started |= ImGui::IsItemActivated();
    edit.finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushItemWidth(80.0f);
    edit.changed |= ImGui::DragFloat(std::format("##{}Z", label).c_str(), &vec.z, 0.1f, 0, 0, "%.4f");
    edit.started |= ImGui::IsItemActivated();
    edit.finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopItemWidth();

    ImGui::PopID();

    return edit;
}

auto imgui_drag_vec3i(
    std::string_view label, vec3i& vec, float32 label_offset
) -> bool {
    constexpr float32 field_width = 60.f;
    constexpr float32 drag_speed  = 0.1f;

    bool changed = false;

    const auto field_id = std::format("##drag_vec3i_{}", label);
    ImGui::PushID(field_id.c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_offset);

    ImGui::SetNextItemWidth(field_width);
    changed |= ImGui::DragInt("##X", &vec.x, drag_speed);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(field_width);
    changed |= ImGui::DragInt("##Y", &vec.y, drag_speed);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(field_width);
    changed |= ImGui::DragInt("##Z", &vec.z, drag_speed);

    ImGui::PopID();

    return changed;
}

auto collect_asset_refs(
    const std::filesystem::path& dir, std::string_view extension
) -> std::vector<asset::asset_ref> {
    namespace fs = std::filesystem;

    std::vector<asset::asset_ref> refs;

    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return refs;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != extension) {
            continue;
        }

        const auto relative =
            fs::relative(entry.path(), fs::path{app_state::asset_root_name}, ec);
        refs.emplace_back(
            ec ? entry.path().filename().generic_string() : relative.generic_string()
        );
    }

    return refs;
}

}  // namespace vw::sculptor
