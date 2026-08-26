module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

create_entity_modal::create_entity_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto create_entity_modal::open() -> void {
    need_open_ = true;
    name_        = std::format("new entity {}", state_->scene.name_to_entity.size());
    with_model_  = false;
    with_socket_ = false;
    category_    = selected_model_category(*engine_, *state_);
}

auto create_entity_modal::render(
    float /*delta_time*/
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Create Entity");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Create Entity", nullptr, dialog_flags)) {
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", error_.c_str());
        }

        imgui_input_text_string("Name", name_);

        ImGui::Checkbox("With Model", &with_model_);
        if (with_model_) {
            imgui_block_set_combo("Blocks", engine_->get_block_registry(), category_);
            imgui_input_int_left("Size X", &size_.x);
            imgui_input_int_left("Size Y", &size_.y);
            imgui_input_int_left("Size Z", &size_.z);
        }

        ImGui::Checkbox("With Socket", &with_socket_);

        if (ImGui::Button("Create")) {
            if (create_entity()) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto create_entity_modal::create_entity() -> bool {
    error_.clear();

    if (name_.empty()) {
        error_ = "Name cannot be empty.";
        return false;
    }
    if (size_.x <= 0 || size_.y <= 0 || size_.z <= 0) {
        error_ = "Size dimensions must be greater than zero.";
        return false;
    }
    if (state_->scene.name_to_entity.contains(name_)) {
        error_ = "An entity with this name already exists.";
        return false;
    }

    create_entity_params params = {
        .name        = name_,
        .parent_name = state_->scene.selected_name,
        .with_model  = with_model_,
        .with_socket = with_socket_,
        .size        = size_,
        .category    = category_,
    };

    auto op = std::make_unique<create_entity_operation>(*engine_, *state_, params);
    op_manager_->execute(std::move(op));

    return true;
}

}  // namespace vw::sculptor
