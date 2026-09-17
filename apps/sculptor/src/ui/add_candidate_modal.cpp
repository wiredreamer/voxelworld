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

add_candidate_modal::add_candidate_modal(
    engine_type& eng, app_state& st, operation_manager& op_manager, asset::model_library& library
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager), library_(&library) {}

auto add_candidate_modal::open(
    const std::string& entity_name
) -> void {
    need_open_   = true;
    entity_name_ = entity_name;
    selected_.clear();

    files_ = collect_asset_refs(app_state::model_dir(), ".voxm");
    files_.append_range(collect_asset_refs(app_state::prefab_dir(), ".vox"));
}

auto add_candidate_modal::render() -> void {
    if (!state_->ui.need_add_candidate_for.empty()) {
        open(state_->ui.need_add_candidate_for);
        state_->ui.need_add_candidate_for.clear();
    }

    if (need_open_) {
        ImGui::OpenPopup("Add Candidate");
        need_open_ = false;
    }

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (!ImGui::BeginPopupModal("Add Candidate", nullptr, flags)) {
        return;
    }

    ImGui::Text("Slot on: %s", entity_name_.c_str());
    ImGui::Separator();

    if (files_.empty()) {
        ImGui::TextDisabled("no volumes and no prefabs to offer");
    }

    if (ImGui::BeginListBox("##candidates", ImVec2(360, 220))) {
        for (const auto& ref : files_) {
            const bool is_selected = ref.str() == selected_;
            if (ImGui::Selectable(ref.str().c_str(), is_selected)) {
                selected_ = ref.str();
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Spacing();

    ImGui::BeginDisabled(selected_.empty());
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

auto add_candidate_modal::confirm_() -> void {
    const auto it = state_->scene.name_to_entity.find(entity_name_);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(it->second)) {
        return;
    }

    auto candidates = world.get<ecs::variant_slot_component>(it->second).get_candidates();
    candidates.emplace_back(selected_);

    op_manager_->execute(
        std::make_unique<set_variant_candidates_operation>(
            *engine_, *state_, *library_,
            set_variant_candidates_params{.name = entity_name_, .candidates = std::move(candidates)}
        )
    );
}

}  // namespace vw::sculptor
