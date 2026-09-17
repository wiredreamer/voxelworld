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

create_keyframe_modal::create_keyframe_modal(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto create_keyframe_modal::open(
    const std::string& track_name
) -> void {
    need_open_  = true;
    track_name_ = track_name;
    error_.clear();

    time_          = state_->anim.timeline_cursor;
    interp_index_  = 0;
    tangent_in_    = 0.f;
    tangent_out_   = 1.f;
    property_index_ = static_cast<int>(state_->anim.selected_property);

    if (state_->scene.name_to_entity.contains(track_name)) {
        auto ent    = state_->scene.name_to_entity[track_name];
        auto& world = engine_->get_world();
        if (world.has<ecs::transform_component>(ent)) {
            auto& tc = world.get<ecs::transform_component>(ent);

            auto prop = static_cast<asset::animation_property>(property_index_);
            if (prop == asset::animation_property::position) {
                value_vec3f_ = tc.get_position();
            } else if (prop == asset::animation_property::rotation) {
                auto rot       = tc.get_rotation_euler();
                value_euler_deg_ = {
                    math::degrees(rot.x), math::degrees(rot.y), math::degrees(rot.z)
                };
            } else {
                value_vec3f_ = tc.get_scale();
            }
        }
    }
}

auto create_keyframe_modal::render(
    float
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Add Keyframe");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Add Keyframe", nullptr, dialog_flags)) {
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", error_.c_str());
        }

        ImGui::TextDisabled("Track: %s", track_name_.c_str());

        const char* prop_names[] = {"Position", "Rotation", "Scale", "Origin"};
        int prev_prop            = property_index_;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Property");
        ImGui::SameLine(110.f);
        ImGui::PushItemWidth(120.f);
        ImGui::Combo("##Property", &property_index_, prop_names, 4);
        ImGui::PopItemWidth();

        if (prev_prop != property_index_ && state_->scene.name_to_entity.contains(track_name_)) {
            auto ent    = state_->scene.name_to_entity[track_name_];
            auto& world = engine_->get_world();
            if (world.has<ecs::transform_component>(ent)) {
                auto& tc  = world.get<ecs::transform_component>(ent);
                auto prop = static_cast<asset::animation_property>(property_index_);
                if (prop == asset::animation_property::position) {
                    value_vec3f_ = tc.get_position();
                } else if (prop == asset::animation_property::rotation) {
                    auto rot       = tc.get_rotation_euler();
                    value_euler_deg_ = {
                        math::degrees(rot.x), math::degrees(rot.y), math::degrees(rot.z)
                    };
                } else {
                    value_vec3f_ = tc.get_scale();
                }
            }
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Time");
        ImGui::SameLine(110.f);
        ImGui::PushItemWidth(120.f);
        ImGui::DragFloat("##Time", &time_, 0.01f, 0.f, 100.f, "%.3f");
        ImGui::PopItemWidth();

        auto prop = static_cast<asset::animation_property>(property_index_);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Value");
        ImGui::SameLine(110.f);
        if (prop == asset::animation_property::rotation) {
            ImGui::PushItemWidth(60.f);
            ImGui::DragFloat("##valX", &value_euler_deg_.x, 0.1f, 0, 0, "%.2f");
            ImGui::SameLine();
            ImGui::DragFloat("##valY", &value_euler_deg_.y, 0.1f, 0, 0, "%.2f");
            ImGui::SameLine();
            ImGui::DragFloat("##valZ", &value_euler_deg_.z, 0.1f, 0, 0, "%.2f");
            ImGui::PopItemWidth();
        } else {
            ImGui::PushItemWidth(60.f);
            ImGui::DragFloat("##valX", &value_vec3f_.x, 0.1f, 0, 0, "%.4f");
            ImGui::SameLine();
            ImGui::DragFloat("##valY", &value_vec3f_.y, 0.1f, 0, 0, "%.4f");
            ImGui::SameLine();
            ImGui::DragFloat("##valZ", &value_vec3f_.z, 0.1f, 0, 0, "%.4f");
            ImGui::PopItemWidth();
        }

        const char* interp_names[] = {"Linear", "Step", "Ease In", "Ease Out", "Ease In Out"};
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Interpolation");
        ImGui::SameLine(110.f);
        ImGui::PushItemWidth(120.f);
        ImGui::Combo("##Interpolation", &interp_index_, interp_names, 5);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Tangent In");
        ImGui::SameLine(110.f);
        ImGui::PushItemWidth(80.f);
        ImGui::DragFloat("##TangentIn", &tangent_in_, 0.01f, 0.f, 1.f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Tangent Out");
        ImGui::SameLine(110.f);
        ImGui::PushItemWidth(80.f);
        ImGui::DragFloat("##TangentOut", &tangent_out_, 0.01f, 0.f, 1.f, "%.2f");
        ImGui::PopItemWidth();

        if (ImGui::Button("Create")) {
            if (create_keyframe()) {
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

auto create_keyframe_modal::create_keyframe() -> bool {
    error_.clear();

    if (track_name_.empty()) {
        error_ = "No track selected.";
        return false;
    }

    if (state_->anim.selected_clip_name.empty()) {
        error_ = "No clip selected.";
        return false;
    }

    auto& clip_registry = engine_->get_world().resource<asset::animation_clip_registry>();
    auto clip           = clip_registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        error_ = "Clip not found.";
        return false;
    }

    auto prop  = static_cast<asset::animation_property>(property_index_);
    auto interp = static_cast<math::interpolation_type>(interp_index_);

    keyframe_value kf_val;
    if (prop == asset::animation_property::rotation) {
        vec3f euler_rad{
            math::radians(value_euler_deg_.x),
            math::radians(value_euler_deg_.y),
            math::radians(value_euler_deg_.z),
        };
        asset::keyframe_quat kf;
        kf.time        = time_;
        kf.value       = math::euler_to_quat(euler_rad);
        kf.interp      = interp;
        kf.tangent_in  = tangent_in_;
        kf.tangent_out = tangent_out_;
        kf_val         = kf;
    } else {
        asset::keyframe_vec3f kf;
        kf.time        = time_;
        kf.value       = value_vec3f_;
        kf.interp      = interp;
        kf.tangent_in  = tangent_in_;
        kf.tangent_out = tangent_out_;
        kf_val         = kf;
    }

    if (!clip->has_track(track_name_)) {
        add_track_params params = {
            .clip_name  = state_->anim.selected_clip_name,
            .track_name = track_name_,
            .property   = prop,
            .keyframe   = kf_val,
        };
        auto op = std::make_unique<add_track_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));
    } else {
        add_keyframe_params params = {
            .clip_name  = state_->anim.selected_clip_name,
            .track_name = track_name_,
            .property   = prop,
            .keyframe   = kf_val,
        };
        auto op = std::make_unique<add_keyframe_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));
    }

    state_->anim.selected_track_name = track_name_;
    state_->anim.selected_property   = prop;

    return true;
}

}  // namespace vw::sculptor
