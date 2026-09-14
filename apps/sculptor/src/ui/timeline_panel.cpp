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

timeline_panel::timeline_panel(
    engine_type& eng,
    app_state& st,
    operation_manager& op_manager,
    clip_service& clip_svc,
    keyframe_service& kf_svc
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , clip_service_(&clip_svc)
    , keyframe_service_(&kf_svc)
    , create_kf_modal_(eng, st, op_manager)
    , delete_track_modal_(eng, st, op_manager) {}

auto timeline_panel::render(
    float delta_time
) -> void {
    keyframe_clicked_ = false;

    const auto& clip_registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip           = clip_registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        state_->ui.show_timeline = false;
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto pos                = ImVec2{
        viewport->WorkPos.x + 10.f,
        viewport->WorkPos.y + viewport->WorkSize.y  //
            - state_->ui.left_bottom_voffset        //
            - 10.f
    };
    const auto size = ImVec2{
        viewport->WorkSize.x - 20.f,  //
        state_->ui.bottom_panel_height - 10.f
    };
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoCollapse |          //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_NoMove |              //
        ImGuiWindowFlags_NoResize |            //
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    const auto title = std::format("Timeline - {}###Timeline", state_->anim.selected_clip_name);
    bool still_open  = state_->ui.show_timeline;
    ImGui::Begin(title.c_str(), &still_open, window_flags);

    if (!still_open && state_->ui.show_timeline) {
        state_->anim.selected_track_name.clear();
        state_->anim.selected_keyframe_id = asset::invalid_keyframe_id;
        state_->ui.show_timeline          = false;
    }

    const bool clip_changed = prev_clip_name_ != state_->anim.selected_clip_name;
    if (clip_changed) {
        prev_clip_name_ = state_->anim.selected_clip_name;
    }

    if (is_current_layer_playing()) {
        const auto root_ent  = state_->scene.name_to_entity[state_->scene.root_name];
        const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
        const auto& player =
            engine_->get_world().get<ecs::animation_player_component>(root_ent);
        state_->anim.timeline_cursor = player.get_layer(layer_idx).time;
        prev_cursor_time_            = state_->anim.timeline_cursor;
    } else if (clip_changed) {
        if (is_clip_on_layer()) {
            const auto root_ent  = state_->scene.name_to_entity[state_->scene.root_name];
            const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
            const auto& player =
                engine_->get_world().get<ecs::animation_player_component>(root_ent);
            state_->anim.timeline_cursor = player.get_layer(layer_idx).time;
        } else {
            state_->anim.timeline_cursor = 0.f;
        }
        prev_cursor_time_ = state_->anim.timeline_cursor;
    }

    float clip_duration = clip->get_duration();
    if (clip_duration <= 0.f) {
        clip_duration = 1.f;
    }

    if (state_->anim.need_step_forward) {
        state_->anim.need_step_forward = false;
        const float step               = clip_duration * 0.01f;
        state_->anim.timeline_cursor = std::min(state_->anim.timeline_cursor + step, clip_duration);
    }
    if (state_->anim.need_step_backward) {
        state_->anim.need_step_backward = false;
        const float step                = clip_duration * 0.01f;
        state_->anim.timeline_cursor    = std::max(state_->anim.timeline_cursor - step, 0.f);
    }

    const bool cursor_changed =
        std::abs(state_->anim.timeline_cursor - prev_cursor_time_) > 0.0001f;

    if (!is_current_layer_playing() && (cursor_changed || state_->anim.need_apply_pose)) {
        if (const auto root = try_get_root_entity()) {
            ensure_clip_on_layer(*root);
            auto& anim_sys       = engine_->get_world().system<ecs::animation_system>();
            const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
            auto player          = anim_sys.modify_player(*root);
            player.layer(layer_idx).set_time(state_->anim.timeline_cursor);
            player.apply_pose();
        }
        prev_cursor_time_            = state_->anim.timeline_cursor;
        state_->anim.need_apply_pose = false;
    }

    render_toolbar(clip_duration);
    ImGui::Separator();
    render_tracks();

    create_kf_modal_.render(delta_time);
    delete_track_modal_.render(delta_time);

    state_->ui.left_bottom_voffset += ImGui::GetWindowHeight() + 10.f;

    ImGui::End();
}

auto timeline_panel::render_toolbar(
    float clip_duration
) -> void {
    const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    const auto clip      = registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    const bool can_add_track = !state_->scene.selected_name.empty() && clip &&
        !clip->has_track(state_->scene.selected_name);
    if (can_add_track) {
        if (ImGui::Button("Add Track")) {
            add_track_params params = {
                .clip_name  = state_->anim.selected_clip_name,
                .track_name = state_->scene.selected_name,
            };
            auto op = std::make_unique<add_track_operation>(*engine_, *state_, params);
            op_manager_->execute(std::move(op));

            state_->anim.selected_track_name = state_->scene.selected_name;
            state_->anim.expanded_tracks.insert(state_->scene.selected_name);
        }
        ImGui::SameLine();
    }

    bool can_add_kf = !state_->anim.selected_track_name.empty() &&
        !state_->anim.selected_clip_name.empty() &&
        clip->has_track(state_->anim.selected_track_name);
    if (can_add_kf) {
        if (ImGui::Button("Add Keyframe")) {
            create_kf_modal_.open(state_->anim.selected_track_name);
        }
        ImGui::SameLine();
    }

    if (can_add_track || can_add_kf) {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    render_playback_controls(clip);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    render_clip_blend_controls_();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    auto& cs = state_->anim.get_clip_settings_mut(state_->anim.selected_clip_name);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Speed");
    ImGui::SameLine();
    ImGui::PushItemWidth(60.f);
    const bool speed_changed =
        ImGui::DragFloat("##Speed", &cs.playback_speed, 0.01f, 0.1f, 5.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Loop");
    ImGui::SameLine();
    ImGui::PushItemWidth(80.f);
    constexpr std::array loop_modes = {"Once", "Loop", "Ping-Pong"};
    int loop_index                  = static_cast<int>(cs.loop_mode);
    const bool loop_changed =
        ImGui::Combo("##Loop", &loop_index, loop_modes.data(), static_cast<int32>(loop_modes.size()));
    if (loop_changed) {
        cs.loop_mode = static_cast<asset::animation_loop_mode>(loop_index);
    }
    ImGui::PopItemWidth();

    const bool is_root_valid = !state_->scene.root_name.empty() &&
        state_->scene.name_to_entity.contains(state_->scene.root_name);
    if ((speed_changed || loop_changed) && is_root_valid) {
        const auto root_ent  = state_->scene.name_to_entity[state_->scene.root_name];
        const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
        auto& world          = engine_->get_world();
        if (world.has<ecs::animation_player_component>(root_ent)) {
            auto& anim_sys = world.system<ecs::animation_system>();
            if (speed_changed) {
                anim_sys.modify_player(root_ent).layer(layer_idx).set_playback_speed(
                    cs.playback_speed
                );
            }
            if (loop_changed) {
                anim_sys.modify_player(root_ent).layer(layer_idx).set_loop_mode(cs.loop_mode);
            }
        }
    }

    ImGui::SameLine();
    ImGui::Text("%.2f / %.2fs", state_->anim.timeline_cursor, clip_duration);

    ImGui::SameLine();
    const float avail      = ImGui::GetContentRegionAvail().x;
    constexpr float zoom_w = 120.f;
    if (avail > zoom_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - zoom_w);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Zoom");
    ImGui::SameLine();
    ImGui::PushItemWidth(60.f);
    ImGui::DragFloat("##Zoom", &zoom_percent_, 1.f, 100.f, 1000.f, "%.0f%%");
    ImGui::PopItemWidth();
    zoom_percent_ = std::clamp(zoom_percent_, 100.f, 1000.f);
}

auto timeline_panel::render_tracks() -> void {
    const auto& clip_registry = engine_->get_world().resource<asset::animation_clip_registry>();

    const auto clip = clip_registry.get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    float clip_duration = clip->get_duration();
    if (clip_duration <= 0.f) {
        clip_duration = 1.f;
    }

    ImGui::BeginChild(
        "TracksArea", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar
    );

    constexpr float label_width = 150.f;
    constexpr float end_padding = 60.f;
    const float available_width = ImGui::GetContentRegionAvail().x;

    float base_track_width = available_width - label_width;
    base_track_width       = std::max(base_track_width, 100.f);

    const float usable_track_width = base_track_width - end_padding;
    float track_area_width         = usable_track_width * (zoom_percent_ / 100.f);

    float max_scroll = std::max(0.f, track_area_width - usable_track_width);
    scroll_offset_   = std::clamp(scroll_offset_, 0.f, max_scroll);

    if (ImGui::IsWindowHovered() && std::abs(ImGui::GetIO().MouseWheel) > 0.f &&
        ImGui::GetIO().KeyCtrl) {
        float old_zoom = zoom_percent_;
        zoom_percent_ += ImGui::GetIO().MouseWheel * 10.f;
        zoom_percent_ = std::clamp(zoom_percent_, 100.f, 1000.f);

        float new_track_width = usable_track_width * (zoom_percent_ / 100.f);
        float new_max_scroll  = std::max(0.f, new_track_width - usable_track_width);

        if (old_zoom > 100.f) {
            scroll_offset_ = scroll_offset_ * (zoom_percent_ / old_zoom);
        }
        scroll_offset_   = std::clamp(scroll_offset_, 0.f, new_max_scroll);
        track_area_width = new_track_width;
        max_scroll       = new_max_scroll;
    }

    if (ImGui::IsWindowHovered() && std::abs(ImGui::GetIO().MouseWheel) > 0.f &&
        !ImGui::GetIO().KeyCtrl && max_scroll > 0.f) {
        scroll_offset_ -= ImGui::GetIO().MouseWheel * 30.f;
        scroll_offset_ = std::clamp(scroll_offset_, 0.f, max_scroll);
    }

    ImGui::Columns(2, "timeline_columns", false);
    ImGui::SetColumnWidth(0, label_width);

    const float time_ruler_y = ImGui::GetCursorScreenPos().y;

    ImGui::TextDisabled("Tracks");
    ImGui::NextColumn();

    const ImVec2 ruler_start = ImGui::GetCursorScreenPos();
    const float ruler_width  = usable_track_width;

    render_time_ruler(vec2f{ruler_start.x, ruler_start.y}, ruler_width, track_area_width, clip_duration);
    ImGui::NextColumn();

    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);

    auto& tracks = clip->get_tracks();
    for (const auto& track : tracks) {
        render_track_row(track, usable_track_width, clip_duration, scroll_offset_);
    }

    const float tracks_end_y = ImGui::GetCursorScreenPos().y;
    const float track_area_x = ruler_start.x;

    ImGui::Columns(1);

    draw_list->ChannelsSetCurrent(0);
    render_playhead(
        track_area_x, usable_track_width, clip_duration, time_ruler_y, tracks_end_y, scroll_offset_
    );
    draw_list->ChannelsMerge();

    render_scrollbar(usable_track_width, track_area_width, max_scroll);

    ImGui::EndChild();
}

auto timeline_panel::render_track_row(
    const asset::animation_track& track,
    float track_area_width,
    float clip_duration,
    float scroll_offset
) -> void {
    const auto& target           = track.get_target_name();
    const bool is_expanded       = state_->anim.expanded_tracks.contains(target);
    const bool is_track_selected = (state_->anim.selected_track_name == target);

    ImGuiTreeNodeFlags node_flags =       //
        ImGuiTreeNodeFlags_OpenOnArrow |  //
        ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (is_track_selected) {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (is_expanded) {
        node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const auto node_id = std::format("{}##track", target);
    const bool opened  = ImGui::TreeNodeEx(node_id.c_str(), node_flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        state_->anim.selected_track_name = target;
    }

    render_track_context_menu(target);

    if (opened != is_expanded) {
        if (opened) {
            state_->anim.expanded_tracks.insert(target);
        } else {
            state_->anim.expanded_tracks.erase(target);
        }
    }

    ImGui::NextColumn();

    if (!opened) {
        constexpr asset::animation_property props[] = {
            asset::animation_property::position,
            asset::animation_property::rotation,
            asset::animation_property::scale
        };

        for (auto prop : props) {
            if (auto* channel_var = track.get_channel(prop)) {
                render_keyframe_markers(
                    *channel_var, target, prop, track_area_width, clip_duration, scroll_offset
                );
            }
        }
        ImGui::Dummy(ImVec2(track_area_width, 16.f));
    } else {
        ImGui::Dummy(ImVec2(track_area_width, 2.f));
    }

    ImGui::NextColumn();

    if (opened) {
        render_expanded_channels(track, target, track_area_width, clip_duration, scroll_offset);
        ImGui::TreePop();
    }
}

auto timeline_panel::is_current_layer_playing() const -> bool {
    auto root = try_get_root_entity();
    if (!root) {
        return false;
    }
    auto& world = engine_->get_world();
    if (!world.has<ecs::animation_player_component>(*root)) {
        return false;
    }
    const auto& player = world.get<ecs::animation_player_component>(*root);
    const auto idx     = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
    if (!player.has_layer(idx)) {
        return false;
    }
    const auto& layer = player.get_layer(idx);
    return layer.state == asset::animation_state::playing && layer.clip &&
        layer.clip->get_name() == state_->anim.selected_clip_name;
}

auto timeline_panel::try_get_root_entity() const -> std::optional<ecs::entity> {
    if (state_->scene.root_name.empty() ||
        !state_->scene.name_to_entity.contains(state_->scene.root_name)) {
        return std::nullopt;
    }
    return state_->scene.name_to_entity[state_->scene.root_name];
}

auto timeline_panel::handle_pause(
    ecs::entity root
) const -> void {
    auto& world = engine_->get_world();
    if (world.has<ecs::animation_player_component>(root)) {
        const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
        world.system<ecs::animation_system>().modify_player(root).layer(layer_idx).pause();
    }
}

auto timeline_panel::handle_play(
    ecs::entity root, const std::shared_ptr<asset::animation_clip>& clip
) const -> void {
    auto& world = engine_->get_world();

    if (!world.has<ecs::animation_player_component>(root)) {
        world.modify(root).with<ecs::animation_player_component>();
    }

    auto& anim_sys       = world.system<ecs::animation_system>();
    const auto& player   = world.get<ecs::animation_player_component>(root);
    const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);

    auto layer = anim_sys.modify_player(root).layer(layer_idx);

    const bool is_same_clip = player.has_layer(layer_idx) && player.get_layer(layer_idx).clip &&
        player.get_layer(layer_idx).clip->get_name() == state_->anim.selected_clip_name;

    const auto& cs = state_->anim.get_clip_settings(state_->anim.selected_clip_name);

    if (is_same_clip && player.get_layer(layer_idx).state == asset::animation_state::paused) {
        layer.set_playback_speed(cs.playback_speed);
        layer.set_loop_mode(cs.loop_mode);
        layer.resume();
    } else {
        const auto& bt           = cs.blend_transition;
        const bool has_prev_clip = player.has_layer(layer_idx) && player.get_layer(layer_idx).clip;

        if (layer_idx > 0 && cs.fade_in.duration > 0.f) {
            layer.blend_to(
                clip, has_prev_clip && bt.duration > 0.f ? std::optional{bt} : std::nullopt
            );
            layer.play(cs.fade_in);
        } else {
            layer.blend_to(clip, bt.duration > 0.f ? std::optional{bt} : std::nullopt);
            layer.play();
        }
        layer.set_fade_out(cs.fade_out);
        layer.set_playback_speed(cs.playback_speed);
        layer.set_loop_mode(cs.loop_mode);
    }
}

auto timeline_panel::handle_stop(
    ecs::entity root
) const -> void {
    auto& world          = engine_->get_world();
    const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
    if (world.has<ecs::animation_player_component>(root)) {
        world.system<ecs::animation_system>().modify_player(root).layer(layer_idx).stop();
    }
    state_->anim.timeline_cursor = 0.f;
}

auto timeline_panel::render_playback_controls(
    const std::shared_ptr<asset::animation_clip>& clip
) -> void {
    const bool playing     = is_current_layer_playing();
    const char* play_label = playing ? "||" : ">";
    if (ImGui::Button(play_label)) {
        if (const auto root = try_get_root_entity()) {
            playing ? handle_pause(*root) : handle_play(*root, clip);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("[]")) {
        if (const auto root = try_get_root_entity()) {
            handle_stop(*root);
        } else {
            state_->anim.timeline_cursor = 0.f;
        }
    }
}

auto timeline_panel::render_clip_blend_controls_() const -> void {
    constexpr std::array interp_names = {
        "Linear", "Step", "Ease In", "Ease Out", "Ease In/Out", "Cubic Bezier"
    };

    auto& cs = state_->anim.get_clip_settings_mut(state_->anim.selected_clip_name);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Blend");
    ImGui::SameLine();
    ImGui::PushItemWidth(60.f);
    ImGui::DragFloat("##BlendDur", &cs.blend_transition.duration, 0.01f, 0.f, 10.f, "%.2fs");
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(80.f);
    int interp = static_cast<int>(cs.blend_transition.interp);
    if (ImGui::Combo(
            "##BlendInterp", &interp, interp_names.data(), static_cast<int32>(interp_names.size())
        )) {
        cs.blend_transition.interp = static_cast<math::interpolation_type>(interp);
    }
    ImGui::PopItemWidth();
}

auto timeline_panel::render_time_ruler(
    vec2f ruler_start, float ruler_width, float track_area_width, float clip_duration
) const -> void {
    auto* draw_list = ImGui::GetWindowDrawList();

    const float visible_start = scroll_offset_ / track_area_width * clip_duration;
    const float visible_end   = (scroll_offset_ + ruler_width) / track_area_width * clip_duration;
    const float visible_range = visible_end - visible_start;

    constexpr float min_tick_spacing = 60.f;
    const int tick_count = std::max(2, static_cast<int>(ruler_width / min_tick_spacing));

    const float time_per_tick = visible_range / static_cast<float>(tick_count);

    int precision = 1;
    if (time_per_tick < 0.001f) {
        precision = 4;
    } else if (time_per_tick < 0.01f) {
        precision = 3;
    } else if (time_per_tick < 0.1f) {
        precision = 2;
    }

    const auto fmt = std::format("{{:.{}f}}", precision);

    for (int i = 0; i <= tick_count; ++i) {
        const float t    = static_cast<float>(i) / static_cast<float>(tick_count);
        const float time = visible_start + (t * visible_range);
        const float x    = ruler_start.x + (t * ruler_width);
        draw_list->AddLine(
            ImVec2(x, ruler_start.y), ImVec2(x, ruler_start.y + 12.f), IM_COL32(180, 180, 180, 255)
        );
        auto label = std::vformat(fmt, std::make_format_args(time));
        draw_list->AddText(
            ImVec2(x + 2, ruler_start.y), IM_COL32(180, 180, 180, 255), label.c_str()
        );
    }
    ImGui::Dummy(ImVec2(ruler_width, 16.f));
}

auto timeline_panel::render_scrollbar(
    float usable_track_width, float track_area_width, float max_scroll
) -> void {
    if (max_scroll <= 0.f) {
        scrollbar_dragging_ = false;
        return;
    }

    constexpr float bar_height = 10.f;

    const float bar_width = ImGui::GetContentRegionAvail().x;
    const float ratio     = usable_track_width / track_area_width;
    const float thumb_w   = std::max(20.f, bar_width * ratio);
    const float thumb_x   = (scroll_offset_ / max_scroll) * (bar_width - thumb_w);

    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        bar_pos, ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_height), IM_COL32(60, 60, 60, 255)
    );

    bool thumb_hovered = false;
    ImVec2 mouse       = ImGui::GetMousePos();
    if (mouse.x >= bar_pos.x + thumb_x && mouse.x <= bar_pos.x + thumb_x + thumb_w &&
        mouse.y >= bar_pos.y && mouse.y <= bar_pos.y + bar_height) {
        thumb_hovered = true;
    }

    ImU32 thumb_color = thumb_hovered || scrollbar_dragging_ ?
        IM_COL32(180, 180, 180, 255) :
        IM_COL32(120, 120, 120, 255);

    dl->AddRectFilled(
        ImVec2(bar_pos.x + thumb_x, bar_pos.y),
        ImVec2(bar_pos.x + thumb_x + thumb_w, bar_pos.y + bar_height),
        thumb_color,
        3.f
    );

    ImGui::InvisibleButton("##scrollbar", ImVec2(bar_width, bar_height));

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (thumb_hovered) {
            scrollbar_dragging_     = true;
            scrollbar_drag_start_   = mouse.x;
            scrollbar_scroll_start_ = scroll_offset_;
        } else {
            const float click_ratio = (mouse.x - bar_pos.x) / bar_width;
            scroll_offset_          = click_ratio * max_scroll;
            scroll_offset_          = std::clamp(scroll_offset_, 0.f, max_scroll);
        }
    }

    if (scrollbar_dragging_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float delta        = mouse.x - scrollbar_drag_start_;
            const float scroll_range = bar_width - thumb_w;
            if (scroll_range > 0.f) {
                scroll_offset_ = scrollbar_scroll_start_ + (delta / scroll_range * max_scroll);
                scroll_offset_ = std::clamp(scroll_offset_, 0.f, max_scroll);
            }
        } else {
            scrollbar_dragging_ = false;
        }
    }
}

auto timeline_panel::render_track_context_menu(
    const std::string& target
) -> void {
    auto ctx_id = std::format("TrackCtx_{}", target);
    if (ImGui::BeginPopupContextItem(ctx_id.c_str())) {
        state_->anim.selected_track_name = target;
        if (ImGui::MenuItem("Add Keyframe...")) {
            create_kf_modal_.open(target);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Track...")) {
            delete_track_modal_.open(target);
        }
        ImGui::EndPopup();
    }
}

auto timeline_panel::render_expanded_channels(
    const asset::animation_track& track,
    const std::string& target,
    float track_area_width,
    float clip_duration,
    float scroll_offset
) -> void {
    for (int i = 0; i < 3; ++i) {
        constexpr std::array prop_names{"Position", "Rotation", "Scale"};
        constexpr std::array props = {
            asset::animation_property::position,
            asset::animation_property::rotation,
            asset::animation_property::scale
        };

        const auto prop = props[i];
        const bool has  = track.has_channel(prop);

        (has ? ImGui::Text : ImGui::TextDisabled)("  %s", prop_names[i]);

        auto sub_ctx_id = std::format("SubTrackCtx_{}_{}", target, i);
        if (ImGui::BeginPopupContextItem(sub_ctx_id.c_str())) {
            auto label = std::format("Add {} Keyframe...", prop_names[i]);
            if (ImGui::MenuItem(label.c_str())) {
                state_->anim.selected_property = prop;
                create_kf_modal_.open(target);
            }
            ImGui::EndPopup();
        }
        ImGui::NextColumn();

        if (has) {
            if (auto* channel_var = track.get_channel(prop)) {
                render_keyframe_markers(
                    *channel_var, target, prop, track_area_width, clip_duration, scroll_offset
                );
            }
        }
        ImGui::Dummy(ImVec2(track_area_width, 16.f));
        ImGui::NextColumn();
    }
}

auto timeline_panel::render_keyframe_markers(
    const asset::animation_channel_variant& channel_var,
    const std::string& track_name,
    asset::animation_property prop,
    float track_width,
    float clip_duration,
    float scroll_offset
) -> void {
    auto* draw_list   = ImGui::GetWindowDrawList();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    float row_height  = 16.f;

    float scale = track_width * (zoom_percent_ / 100.f);

    std::visit(
        [&](const auto& channel) {
            const auto& keyframes = channel.get_keyframes();
            for (const auto& kf : keyframes) {
                float pixel_pos = (kf.time / clip_duration) * scale - scroll_offset;

                if (pixel_pos < -10.f || pixel_pos > track_width + 10.f) {
                    continue;
                }

                float x = cursor_pos.x + pixel_pos;
                float y = cursor_pos.y + row_height * 0.5f;

                bool is_selected = (state_->anim.selected_track_name == track_name) &&
                    (state_->anim.selected_property == prop) &&
                    (kf.id() == state_->anim.selected_keyframe_id);

                ImU32 color =
                    is_selected ? IM_COL32(255, 200, 50, 255) : IM_COL32(200, 200, 200, 255);

                constexpr float diamond_size = 5.f;
                draw_list->AddQuadFilled(
                    ImVec2(x, y - diamond_size),
                    ImVec2(x + diamond_size, y),
                    ImVec2(x, y + diamond_size),
                    ImVec2(x - diamond_size, y),
                    color
                );

                ImVec2 mouse = ImGui::GetMousePos();
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None) &&
                    std::abs(mouse.x - x) < diamond_size + 2.f &&
                    std::abs(mouse.y - y) < diamond_size + 2.f) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        state_->anim.selected_track_name  = track_name;
                        state_->anim.selected_property    = prop;
                        state_->anim.selected_keyframe_id = kf.id();
                        keyframe_clicked_                 = true;
                    }

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        state_->anim.selected_track_name  = track_name;
                        state_->anim.selected_property    = prop;
                        state_->anim.selected_keyframe_id = kf.id();
                        ImGui::OpenPopup("KeyframeContextMenu");
                    }
                }
            }
        },
        channel_var
    );

    if (ImGui::BeginPopup("KeyframeContextMenu")) {
        if (ImGui::MenuItem("Delete Keyframe")) {
            keyframe_service_->delete_keyframe();
        }
        ImGui::EndPopup();
    }
}

auto timeline_panel::render_playhead(
    float track_area_x,
    float track_width,
    float clip_duration,
    float area_top,
    float area_bottom,
    float scroll_offset
) const -> void {
    if (clip_duration <= 0.f) {
        return;
    }

    float scale     = track_width * (zoom_percent_ / 100.f);
    float pixel_pos = (state_->anim.timeline_cursor / clip_duration * scale) - scroll_offset;
    float x         = track_area_x + pixel_pos;

    if (pixel_pos >= -2.f && pixel_pos <= track_width + 2.f) {
        auto* draw_list = ImGui::GetWindowDrawList();

        float zone_left  = track_area_x;
        float zone_right = std::min(x, track_area_x + track_width);
        if (zone_right > zone_left) {
            draw_list->AddRectFilled(
                ImVec2(zone_left, area_top),
                ImVec2(zone_right, area_bottom),
                IM_COL32(255, 80, 80, 30)
            );
        }

        draw_list->AddLine(
            ImVec2(x, area_top), ImVec2(x, area_bottom), IM_COL32(255, 80, 80, 255), 2.0f
        );

        constexpr float tri_size = 6.f;
        draw_list->AddTriangleFilled(
            ImVec2(x - tri_size, area_top),
            ImVec2(x + tri_size, area_top),
            ImVec2(x, area_top + tri_size),
            IM_COL32(255, 80, 80, 255)
        );
    }

    ImVec2 mouse = ImGui::GetMousePos();
    if (!keyframe_clicked_ && ImGui::IsWindowHovered(ImGuiHoveredFlags_None) &&
        mouse.x >= track_area_x && mouse.x <= track_area_x + track_width &&
        mouse.y >= area_top && mouse.y <= area_bottom) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!ImGui::IsAnyItemActive()) {
                float local_x                = mouse.x - track_area_x + scroll_offset;
                float new_time               = (local_x / scale) * clip_duration;
                state_->anim.timeline_cursor = std::clamp(new_time, 0.f, clip_duration);
            }
        }
    }
}

auto timeline_panel::is_clip_on_layer() const -> bool {
    auto root = try_get_root_entity();
    if (!root) {
        return false;
    }
    auto& world = engine_->get_world();
    if (!world.has<ecs::animation_player_component>(*root)) {
        return false;
    }
    const auto& player = world.get<ecs::animation_player_component>(*root);
    const auto idx     = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
    return player.has_layer(idx) && player.get_layer(idx).clip &&
        player.get_layer(idx).clip->get_name() == state_->anim.selected_clip_name;
}

auto timeline_panel::ensure_clip_on_layer(
    ecs::entity root
) const -> void {
    if (is_clip_on_layer()) {
        return;
    }

    auto& world = engine_->get_world();
    auto clip   = world.resource<asset::animation_clip_registry>().get(state_->anim.selected_clip_name);
    if (!clip) {
        return;
    }

    if (!world.has<ecs::animation_player_component>(root)) {
        world.modify(root).with<ecs::animation_player_component>();
    }

    auto& anim_sys       = world.system<ecs::animation_system>();
    const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
    const auto& cs       = state_->anim.get_clip_settings(state_->anim.selected_clip_name);

    auto layer = anim_sys.modify_player(root).layer(layer_idx);
    layer.blend_to(
        clip, cs.blend_transition.duration > 0.f ? std::optional{cs.blend_transition} : std::nullopt
    );
    layer.set_playback_speed(cs.playback_speed);
    layer.set_loop_mode(cs.loop_mode);
    layer.pause();
}

}  // namespace vw::sculptor
