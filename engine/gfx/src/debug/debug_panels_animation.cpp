module;

#include <imgui.h>

module vw.gfx;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

namespace vw::gfx {

namespace {

auto state_text(asset::animation_state state) -> const char* {
    switch (state) {
        case asset::animation_state::stopped: return "stopped";
        case asset::animation_state::playing: return "playing";
        case asset::animation_state::paused: return "paused";
    }

    return "stopped";
}

auto loop_text(asset::animation_loop_mode mode) -> const char* {
    switch (mode) {
        case asset::animation_loop_mode::once: return "once";
        case asset::animation_loop_mode::loop: return "loop";
        case asset::animation_loop_mode::ping_pong: return "ping_pong";
    }

    return "loop";
}

auto clip_name(const std::shared_ptr<asset::animation_clip>& clip) -> const char* {
    return clip ? clip->get_name().c_str() : "-";
}

}  // namespace

auto debug_window::collect_animation_events_(
    float32 delta_time
) -> void {
    debug_clock_seconds_ += delta_time;

    auto& world = engine_->get_world();
    for (auto [ent, player] : world.view<ecs::animation_player_component>()) {
        for (const auto& event : player.get_fired_events()) {
            animation_event_log_.push_back({
                .entity  = ent,
                .seconds = debug_clock_seconds_,
                .layer   = event.layer,
                .clip    = event.clip,
                .name    = event.name,
                .payload = event.payload,
            });
        }
    }

    while (animation_event_log_.size() > animation_event_log_capacity_) {
        animation_event_log_.pop_front();
    }
}

auto debug_window::render_animation_event_log_() -> void {
    ImGui::SeparatorText("events");

    if (ImGui::SmallButton("clear")) {
        std::erase_if(animation_event_log_, [this](const logged_animation_event& logged) {
            return logged.entity == animation_entity_;
        });
    }

    constexpr ImGuiChildFlags log_flags = ImGuiChildFlags_Borders;
    if (!ImGui::BeginChild("##animation_events", ImVec2(420.0f, 160.0f), log_flags)) {
        ImGui::EndChild();
        return;
    }

    bool any = false;
    for (const auto& logged : animation_event_log_ | std::views::reverse) {
        if (logged.entity != animation_entity_) {
            continue;
        }
        any = true;

        const auto text = std::format(
            "{:8.2f}s  L{}  {:<18} {} {}", logged.seconds, logged.layer, logged.name,
            logged.payload, logged.clip
        );
        if (debug_clock_seconds_ - logged.seconds < fresh_event_seconds_) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", text.c_str());
        } else {
            ImGui::TextUnformatted(text.c_str());
        }
    }

    if (!any) {
        ImGui::TextDisabled("no events yet");
    }

    ImGui::EndChild();
}

auto debug_window::render_animation_panel() -> void {
    auto& world = engine_->get_world();

    std::vector<ecs::entity> animated;
    for (auto [ent, fsm, player] :
         world.view<ecs::animation_fsm_component, ecs::animation_player_component>()) {
        animated.push_back(ent);
    }

    if (animated.empty()) {
        ImGui::TextUnformatted("no animated entities");
        return;
    }

    const auto selected = std::ranges::find(animated, animation_entity_);
    if (selected == animated.end()) {
        animation_entity_ = animated.front();
    }

    if (ImGui::BeginCombo("entity", std::format("{}", animation_entity_.index).c_str())) {
        for (const auto ent : animated) {
            const auto label = std::format("{}", ent.index);
            if (ImGui::Selectable(label.c_str(), ent == animation_entity_)) {
                animation_entity_ = ent;
            }
        }
        ImGui::EndCombo();
    }

    auto& registry     = world.registry();
    const auto& fsm    = registry.get<ecs::animation_fsm_component>(animation_entity_);
    const auto& player = registry.get<ecs::animation_player_component>(animation_entity_);

    for (std::size_t index = 0; index < fsm.machine_count(); ++index) {
        ImGui::SeparatorText(std::format("layer {}", index).c_str());

        ImGui::Text("%-10s %s", "state", fsm.get_machine(index).get_current_state().c_str());

        if (!player.has_layer(index)) {
            ImGui::TextUnformatted("not playing yet");
            continue;
        }

        const auto& layer = player.get_layer(index);
        ImGui::Text(
            "%-10s %s (%s, %s, %.2fx)", "clip", clip_name(layer.clip), state_text(layer.state),
            loop_text(layer.loop_mode), layer.playback_speed
        );

        if (layer.is_blending()) {
            const auto fraction = layer.blend_elapsed / layer.blend_transition.duration;
            ImGui::Text("%-10s from %s", "blending", clip_name(layer.blend_prev_clip));
            ImGui::ProgressBar(
                fraction, ImVec2(-1.0f, 0.0f),
                std::format("{:.2f} / {:.2f}s", layer.blend_elapsed,
                            layer.blend_transition.duration)
                    .c_str()
            );
        }

        if (layer.fade_influence < 1.0f || layer.fade_is_out) {
            ImGui::Text(
                "%-10s %.2f%s", "influence", layer.fade_influence, layer.fade_is_out ? " (out)" : ""
            );
        }

        ImGui::Text("%-10s %zu targets", "mask", layer.mask.size());
    }

    render_animation_event_log_();

    ImGui::SeparatorText("parameters");

    const auto machines = world.system<ecs::animation_fsm_system>().modify(animation_entity_);
    const auto declared = fsm.get_declared_parameters();
    const auto entries  = fsm.get_board().entries();

    bool any_trigger = false;
    for (const auto& param : declared) {
        if (param.type != asset::voxf_param_type::trigger) {
            continue;
        }
        if (ImGui::Button(param.name.c_str())) {
            machines.fire_trigger(param.name);
        }
        ImGui::SameLine();
        any_trigger = true;
    }
    if (any_trigger) {
        ImGui::NewLine();
    }

    if (entries.empty()) {
        ImGui::TextUnformatted("none declared");
        return;
    }

    ImGui::TextDisabled("pin holds the value against the game");
    for (const auto& [name, value] : entries) {
        ImGui::PushID(name.c_str());

        bool pinned = fsm.is_pinned(name);
        if (ImGui::Checkbox("##pin", &pinned)) {
            if (pinned) {
                machines.pin_parameter(name, value);
            } else {
                machines.unpin_parameter(name);
            }
        }
        ImGui::SameLine();

        const auto param = std::ranges::find(declared, name, &asset::voxf_param::name);
        const auto type  = param != declared.end() ? param->type : asset::voxf_param_type::real;
        switch (type) {
            case asset::voxf_param_type::boolean: {
                bool on = value != 0.0F;
                if (ImGui::Checkbox(name.c_str(), &on)) {
                    machines.pin_parameter(name, on ? 1.0F : 0.0F);
                }
                break;
            }
            case asset::voxf_param_type::integer: {
                int32 number = static_cast<int32>(value);
                if (ImGui::InputInt(name.c_str(), &number)) {
                    machines.pin_parameter(name, static_cast<float32>(number));
                }
                break;
            }
            case asset::voxf_param_type::real:
            case asset::voxf_param_type::trigger: {
                float32 number = value;
                if (ImGui::DragFloat(name.c_str(), &number, 0.5F)) {
                    machines.pin_parameter(name, number);
                }
                break;
            }
        }

        ImGui::PopID();
    }

    if (ImGui::Button("unpin all")) {
        machines.unpin_all_parameters();
    }
}

}  // namespace vw::gfx
