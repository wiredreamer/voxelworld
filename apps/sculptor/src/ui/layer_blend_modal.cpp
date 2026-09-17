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

layer_blend_modal::layer_blend_modal(
    app_state& st
)
    : state_(&st) {}

auto layer_blend_modal::open() -> void {
    need_open_ = true;

    const auto& cs     = state_->anim.get_clip_settings(state_->anim.selected_clip_name);
    fade_in_duration_  = cs.fade_in.duration;
    fade_in_interp_    = static_cast<int>(cs.fade_in.interp);
    fade_out_duration_ = cs.fade_out.duration;
    fade_out_interp_   = static_cast<int>(cs.fade_out.interp);
}

auto layer_blend_modal::render(
    float
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Layer Blend");
        need_open_ = false;
    }

    constexpr ImGuiWindowFlags dialog_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal("Layer Blend", nullptr, dialog_flags)) {
        constexpr std::array interp_names = {
            "Linear", "Step", "Ease In", "Ease Out", "Ease In/Out", "Cubic Bezier"
        };

        ImGui::Text("Fade In");
        ImGui::PushItemWidth(100.f);
        ImGui::DragFloat("Duration##fade_in", &fade_in_duration_, 0.01f, 0.f, 10.f, "%.2fs");
        ImGui::SameLine();
        ImGui::Combo("Interp##fade_in", &fade_in_interp_, interp_names.data(), static_cast<int32>(interp_names.size()));
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Fade Out");
        ImGui::PushItemWidth(100.f);
        ImGui::DragFloat("Duration##fade_out", &fade_out_duration_, 0.01f, 0.f, 10.f, "%.2fs");
        ImGui::SameLine();
        ImGui::Combo(
            "Interp##fade_out", &fade_out_interp_, interp_names.data(), static_cast<int32>(interp_names.size())
        );
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Apply")) {
            auto& cs         = state_->anim.get_clip_settings_mut(state_->anim.selected_clip_name);
            cs.fade_in.duration  = fade_in_duration_;
            cs.fade_in.interp    = static_cast<math::interpolation_type>(fade_in_interp_);
            cs.fade_out.duration = fade_out_duration_;
            cs.fade_out.interp   = static_cast<math::interpolation_type>(fade_out_interp_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

}  // namespace vw::sculptor
