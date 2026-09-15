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

namespace fs = std::filesystem;

namespace {

auto collect_machines() -> std::vector<asset::asset_ref> {
    std::vector<asset::asset_ref> files;

    std::error_code ec;
    const auto dir = app_state::fsm_dir();
    if (!fs::exists(dir, ec)) {
        return files;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".voxf") {
            continue;
        }

        const auto relative = fs::relative(entry.path(), fs::path{app_state::asset_root_name}, ec);
        files.push_back(asset::asset_ref{
            ec ? entry.path().filename().generic_string() : relative.generic_string()
        });
    }

    return files;
}

}  // namespace

add_machine_modal::add_machine_modal(
    engine_type& eng, app_state& st, operation_manager& op_manager, fsm_service& service
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager), service_(&service) {}

auto add_machine_modal::open() -> void {
    need_open_ = true;
    selected_.clear();
    new_name_.clear();
    files_ = collect_machines();
}

auto add_machine_modal::render() -> void {
    if (state_->ui.need_add_machine_modal) {
        open();
        state_->ui.need_add_machine_modal = false;
    }

    if (need_open_) {
        ImGui::OpenPopup("Add Machine");
        need_open_ = false;
    }

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (!ImGui::BeginPopupModal("Add Machine", nullptr, flags)) {
        return;
    }

    ImGui::TextDisabled("a machine is a layer; order decides which one leads");
    ImGui::Separator();

    if (files_.empty()) {
        ImGui::TextDisabled("no machines yet");
    }

    if (ImGui::BeginListBox("##machines", ImVec2(360, 200))) {
        for (const auto& ref : files_) {
            const bool is_selected = ref.str() == selected_;
            if (ImGui::Selectable(ref.str().c_str(), is_selected)) {
                selected_ = ref.str();
                new_name_.clear();
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("or a new one");
    ImGui::SetNextItemWidth(360.0f);
    imgui_input_text_string("##new_machine", new_name_);
    if (!new_name_.empty()) {
        selected_.clear();
    }

    ImGui::Spacing();

    ImGui::BeginDisabled(selected_.empty() && new_name_.empty());
    if (ImGui::Button("Add")) {
        confirm_();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

auto add_machine_modal::confirm_() -> void {
    auto machines = service_->machines();

    if (!new_name_.empty()) {
        const auto created = service_->create(new_name_);
        if (!created.has_value()) {
            return;
        }
        machines.push_back(*created);
    } else {
        machines.push_back(asset::asset_ref{selected_});
    }

    op_manager_->execute(std::make_unique<set_machines_operation>(
        *engine_, *state_, set_machines_params{.machines = std::move(machines)}
    ));
}

}  // namespace vw::sculptor
