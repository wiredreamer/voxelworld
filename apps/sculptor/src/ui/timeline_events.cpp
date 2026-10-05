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

constexpr float32 event_row_height   = 18.f;
constexpr float32 event_flag_width   = 6.f;
constexpr float32 event_hit_slop     = 4.f;
constexpr float32 event_drag_pixels  = 2.f;
constexpr const char* event_menu_id   = "TimelineEventMenu";
constexpr const char* event_editor_id = "Animation Event";

constexpr ImU32 event_color          = IM_COL32(90, 200, 220, 255);
constexpr ImU32 selected_event_color = IM_COL32(255, 200, 50, 255);
constexpr ImU32 event_label_color    = IM_COL32(170, 220, 230, 255);

}  // namespace

auto timeline_panel::render_event_problems_(
    const asset::animation_clip& clip
) const -> void {
    for (const auto& problem : asset::find_problems(clip)) {
        ImGui::TextColored(ImVec4{1.f, 0.6f, 0.3f, 1.f}, "%s", problem.c_str());
    }
}

auto timeline_panel::render_event_row_(
    const asset::animation_clip& clip, float track_area_width, float clip_duration,
    float scroll_offset
) -> void {
    ImGui::Selectable("Events##event_row", false, ImGuiSelectableFlags_None, ImVec2(0.f, event_row_height));
    if (ImGui::BeginPopupContextItem("EventRowCtx")) {
        if (ImGui::MenuItem("Add Event at Playhead...")) {
            open_event_editor_(
                std::nullopt, asset::animation_event{.time = state_->anim.timeline_cursor}
            );
        }
        ImGui::EndPopup();
    }
    ImGui::SetItemTooltip("Right-click to add an event at the playhead");

    ImGui::NextColumn();

    auto* draw_list    = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float scale  = track_area_width * (zoom_percent_ / 100.f);
    const ImVec2 mouse = ImGui::GetMousePos();
    const float top    = start.y + 1.f;
    const float bottom = start.y + event_row_height - 1.f;

    draw_list->AddRectFilled(
        ImVec2(start.x, top), ImVec2(start.x + track_area_width, bottom), IM_COL32(40, 55, 60, 120)
    );

    const auto& events = clip.get_events();
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        const float pixel = (event.time / clip_duration) * scale - scroll_offset;
        if (pixel < -event_flag_width || pixel > track_area_width + event_flag_width) {
            continue;
        }

        const float x       = start.x + pixel;
        const bool selected = selected_event_ == index;
        const ImU32 color   = selected ? selected_event_color : event_color;

        draw_list->AddLine(ImVec2(x, top), ImVec2(x, bottom), color, 2.f);
        draw_list->AddTriangleFilled(
            ImVec2(x, top), ImVec2(x + event_flag_width, top + event_flag_width * 0.5f),
            ImVec2(x, top + event_flag_width), color
        );
        draw_list->AddText(
            ImVec2(x + event_flag_width + 2.f, top + 1.f), event_label_color, event.name.c_str()
        );

        if (event_drag_ && event_drag_moved_ && drag_event_ == index) {
            const float ghost = start.x + (drag_event_time_ / clip_duration) * scale - scroll_offset;
            draw_list->AddLine(ImVec2(ghost, top), ImVec2(ghost, bottom), selected_event_color, 1.f);
        }

        const bool hovered = ImGui::IsWindowHovered() &&
            std::abs(mouse.x - x) < event_flag_width + event_hit_slop && mouse.y >= top &&
            mouse.y <= bottom;
        if (!hovered) {
            continue;
        }

        ImGui::SetTooltip(
            "%s%s%s  at %.3f s", event.name.c_str(), event.payload.empty() ? "" : " ",
            event.payload.c_str(), event.time
        );

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selected_event_ = index;
            open_event_editor_(index, event);
            keyframe_clicked_ = true;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selected_event_              = index;
            state_->anim.timeline_cursor = event.time;
            keyframe_clicked_            = true;
            event_drag_                  = true;
            event_drag_moved_            = false;
            drag_event_                  = index;
            drag_event_time_             = event.time;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selected_event_  = index;
            need_event_menu_ = true;
        }
    }

    ImGui::Dummy(ImVec2(track_area_width, event_row_height));
    ImGui::NextColumn();
}

auto timeline_panel::update_event_drag_(
    const asset::animation_clip& clip, float clip_duration
) -> void {
    if (!event_drag_) {
        return;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (track_area_width_ <= 0.f) {
            return;
        }
        if (std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x) > event_drag_pixels) {
            event_drag_moved_ = true;
        }
        if (event_drag_moved_) {
            const float local_x = ImGui::GetMousePos().x - track_area_screen_x_ + scroll_offset_;
            drag_event_time_ =
                std::clamp((local_x / track_area_width_) * clip_duration, 0.f, clip.get_duration());
            state_->anim.timeline_cursor = drag_event_time_;
        }
        return;
    }

    if (event_drag_moved_ && drag_event_ < clip.get_events().size()) {
        auto events                    = clip.get_events();
        events[drag_event_].time       = drag_event_time_;
        const asset::animation_event moved = events[drag_event_];
        static_cast<void>(apply_events_(clip, std::move(events), moved));
    }

    event_drag_       = false;
    event_drag_moved_ = false;
}

auto timeline_panel::render_event_menu_(
    const asset::animation_clip& clip
) -> void {
    if (need_event_menu_) {
        need_event_menu_ = false;
        ImGui::OpenPopup(event_menu_id);
    }

    if (!ImGui::BeginPopup(event_menu_id)) {
        return;
    }

    const auto& events = clip.get_events();
    if (!selected_event_ || *selected_event_ >= events.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const std::size_t index = *selected_event_;
    if (ImGui::MenuItem("Edit Event...")) {
        open_event_editor_(index, events[index]);
    }
    if (ImGui::MenuItem("Delete Event")) {
        auto remaining = events;
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
        selected_event_.reset();
        static_cast<void>(apply_events_(clip, std::move(remaining), asset::animation_event{}));
    }
    ImGui::EndPopup();
}

auto timeline_panel::open_event_editor_(
    std::optional<std::size_t> index, const asset::animation_event& event
) -> void {
    edited_event_      = index;
    event_draft_       = event;
    event_draft_error_.clear();
    need_event_editor_ = true;
}

auto timeline_panel::render_event_editor_(
    const asset::animation_clip& clip
) -> void {
    if (need_event_editor_) {
        need_event_editor_ = false;
        ImGui::OpenPopup(event_editor_id);
    }

    if (!ImGui::BeginPopupModal(event_editor_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    imgui_input_text_string("Name", event_draft_.name, 70.f);
    imgui_input_text_string("Payload", event_draft_.payload, 70.f);
    ImGui::SetNextItemWidth(120.f);
    ImGui::DragFloat("Time", &event_draft_.time, 0.005f, 0.f, clip.get_duration(), "%.3f s");
    ImGui::TextDisabled("Names follow hit.start, footstep, cancel.ok; payload is one word or empty");

    if (!event_draft_error_.empty()) {
        ImGui::PushTextWrapPos(420.f);
        ImGui::TextColored(ImVec4{1.f, 0.5f, 0.4f, 1.f}, "%s", event_draft_error_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (ImGui::Button("OK")) {
        auto events = clip.get_events();
        if (edited_event_ && *edited_event_ < events.size()) {
            events[*edited_event_] = event_draft_;
        } else {
            events.push_back(event_draft_);
        }

        const auto applied = apply_events_(clip, std::move(events), event_draft_);
        if (applied) {
            ImGui::CloseCurrentPopup();
        } else {
            event_draft_error_ = applied.error();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

auto timeline_panel::apply_events_(
    const asset::animation_clip& clip, std::vector<asset::animation_event> events,
    const asset::animation_event& to_select
) -> clip_service::outcome {
    const std::string clip_name = clip.get_name();
    auto applied                = clip_service_->set_events(clip_name, std::move(events));
    if (!applied) {
        return applied;
    }

    const auto& now   = clip.get_events();
    const auto picked = std::ranges::find(now, to_select);
    selected_event_   = picked == now.end()
          ? std::nullopt
          : std::optional{static_cast<std::size_t>(picked - now.begin())};
    return applied;
}

}  // namespace vw::sculptor
