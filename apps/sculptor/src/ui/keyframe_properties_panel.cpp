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

keyframe_properties_panel::keyframe_properties_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto keyframe_properties_panel::render(
    float /*delta_time*/
) -> void {
    if (state_->anim.selected_keyframe_id == asset::invalid_keyframe_id ||
        state_->anim.selected_clip_name.empty() || state_->anim.selected_track_name.empty()) {
        return;
    }

    const auto& clip_registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip           = clip_registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    auto* track = clip->get_track(state_->anim.selected_track_name);
    if (!track) {
        return;
    }

    auto* channel_var = track->get_channel(state_->anim.selected_property);
    if (!channel_var) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos         = ImVec2(
        viewport->WorkPos.x + viewport->WorkSize.x - 10,
        viewport->WorkPos.y + state_->ui.right_top_voffset + 10
    );
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_NoMove |              //
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Keyframe Properties", nullptr, window_flags);

    const char* prop_names[] = {"Position", "Rotation", "Scale", "Origin"};
    const int prop_idx       = static_cast<int>(state_->anim.selected_property);
    std::visit(
        [&](const auto& channel) {
            const auto& keyframes = channel.get_keyframes();
            for (const auto& kf : keyframes) {
                if (kf.id() != state_->anim.selected_keyframe_id) {
                    continue;
                }

                ImGui::TextDisabled(
                    "Track: %s, %s, T: %.3f, ID: %u",
                    state_->anim.selected_track_name.c_str(),
                    prop_names[prop_idx],
                    kf.time,
                    kf.id()
                );

                using kf_type    = std::decay_t<decltype(kf)>;
                using value_type = decltype(kf.value);

                auto old_kf = kf;

                float new_time = kf.time;
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Time");
                ImGui::SameLine(110.f);
                ImGui::PushItemWidth(80.f);
                ImGui::DragFloat("##Time_kf", &new_time, 0.01f, 0.f, 100.f, "%.3f");
                ImGui::PopItemWidth();

                bool value_changed = false;
                auto new_value     = kf.value;

                if constexpr (std::is_same_v<value_type, vec3f>) {
                    value_changed = imgui_drag_vec3f("Value", new_value, 110.f);
                } else if constexpr (std::is_same_v<value_type, quat>) {
                    vec3f euler = math::quat_to_euler(kf.value);
                    vec3f euler_deg{
                        math::degrees(euler.x),
                        math::degrees(euler.y),
                        math::degrees(euler.z),
                    };
                    if (imgui_drag_vec3f("Value", euler_deg, 110.f)) {
                        vec3f euler_rad{
                            math::radians(euler_deg.x),
                            math::radians(euler_deg.y),
                            math::radians(euler_deg.z),
                        };
                        new_value     = math::euler_to_quat(euler_rad);
                        value_changed = true;
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Transition to next keyframe");

                int interp_idx             = static_cast<int>(kf.interp);
                const char* interp_names[] = {
                    "Linear", "Step", "Ease In", "Ease Out", "Ease In Out"
                };
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Interpolation");
                ImGui::SameLine(110.f);
                ImGui::PushItemWidth(120.f);
                bool interp_changed = ImGui::Combo("##Interp_kf", &interp_idx, interp_names, 5);
                ImGui::PopItemWidth();

                float new_tangent_in  = kf.tangent_in;
                float new_tangent_out = kf.tangent_out;
                bool tangent_changed  = false;

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Tangent In");
                ImGui::SameLine(110.f);
                ImGui::PushItemWidth(80.f);
                tangent_changed |=
                    ImGui::DragFloat("##TangentIn_kf", &new_tangent_in, 0.01f, 0.f, 1.f, "%.2f");
                ImGui::PopItemWidth();

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Tangent Out");
                ImGui::SameLine(110.f);
                ImGui::PushItemWidth(80.f);
                tangent_changed |=
                    ImGui::DragFloat("##TangentOut_kf", &new_tangent_out, 0.01f, 0.f, 1.f, "%.2f");
                ImGui::PopItemWidth();

                bool time_changed = std::abs(new_time - kf.time) > 0.0001f;

                if (time_changed || value_changed || interp_changed || tangent_changed) {
                    kf_type new_kf;
                    new_kf.time        = new_time;
                    new_kf.value       = value_changed ? new_value : kf.value;
                    new_kf.interp      = static_cast<math::interpolation_type>(interp_idx);
                    new_kf.tangent_in  = new_tangent_in;
                    new_kf.tangent_out = new_tangent_out;

                    modify_keyframe_params mod_params;
                    mod_params.clip_name    = state_->anim.selected_clip_name;
                    mod_params.track_name   = state_->anim.selected_track_name;
                    mod_params.property     = state_->anim.selected_property;
                    mod_params.old_keyframe = keyframe_value(old_kf);
                    mod_params.new_keyframe = keyframe_value(new_kf);
                    auto op =
                        std::make_unique<modify_keyframe_operation>(*engine_, *state_, mod_params);
                    op_manager_->execute(std::move(op));
                }

                ImGui::Spacing();

                if (ImGui::Button("Delete Keyframe")) {
                    remove_keyframe_params rm_params;
                    rm_params.clip_name  = state_->anim.selected_clip_name;
                    rm_params.track_name = state_->anim.selected_track_name;
                    rm_params.property   = state_->anim.selected_property;
                    rm_params.keyframe   = keyframe_value(old_kf);
                    auto op =
                        std::make_unique<remove_keyframe_operation>(*engine_, *state_, rm_params);
                    op_manager_->execute(std::move(op));
                }

                break;
            }
        },
        *channel_var
    );

    ImGui::Dummy({200.0f, 0.0f});

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    ImGui::End();
}


}  // namespace vw::sculptor
