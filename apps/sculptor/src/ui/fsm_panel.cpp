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

constexpr std::array<const char*, 6> compare_names{"==", "!=", "<", "<=", ">", ">="};
constexpr std::array<const char*, 3> playback_names{"once", "loop", "ping_pong"};
constexpr std::array<const char*, 4> param_type_names{"float", "int", "bool", "trigger"};

auto label_width() -> float32 {
    return ImGui::GetFontSize() * 6.0f;
}

auto field_label(const char* text) -> void {
    ImGui::TextUnformatted(text);
    ImGui::SameLine(label_width());
    ImGui::SetNextItemWidth(-1.0f);
}

auto condition_text(const asset::voxf_data& data, const asset::fsm_condition& condition)
    -> std::string {
    const auto it = std::ranges::find(data.params, condition.parameter, &asset::voxf_param::name);
    const bool as_bool =
        it != data.params.end() && it->type == asset::voxf_param_type::boolean;

    const auto value = as_bool ? std::string{condition.value != 0.0F ? "true" : "false"}
                               : std::format("{:.6g}", condition.value);

    return std::format(
        "{} {} {}", condition.parameter, compare_names[static_cast<std::size_t>(condition.compare)],
        value
    );
}

auto rule_summary(const asset::voxf_data& data, const asset::animation_fsm::transition_rule& rule)
    -> std::string {
    std::string text = rule.target_state.empty() ? "(nowhere)" : rule.target_state;

    if (!rule.trigger_name.empty()) {
        text += std::format("  on {}", rule.trigger_name);
    }
    for (const auto& condition : rule.conditions) {
        text += std::format("  when {}", condition_text(data, condition));
    }
    if (rule.blend.duration > 0.0F) {
        text += std::format("  blend {:.2f}", rule.blend.duration);
    }
    if (rule.wait_until_end) {
        text += "  wait end";
    }
    if (rule.wait_until_blend) {
        text += "  wait blend";
    }

    return text;
}

}  // namespace

fsm_panel::fsm_panel(
    app_state& st, operation_manager& op_manager, fsm_service& service
)
    : state_(&st), op_manager_(&op_manager), service_(&service) {}

auto fsm_panel::begin_edit_() -> void {
    if (!editing_) {
        before_  = state_->fsm.data;
        editing_ = true;
    }
}

auto fsm_panel::commit_edit_() -> void {
    if (!editing_) {
        return;
    }

    editing_ = false;
    if (before_ == state_->fsm.data) {
        return;
    }

    op_manager_->execute(std::make_unique<set_fsm_operation>(
        *state_, set_fsm_params{.before = std::move(before_), .after = state_->fsm.data}
    ));
}

auto fsm_panel::render(
    float
) -> void {
    if (!state_->ctx.in_fsm() || !state_->fsm.is_open()) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
    begin_panel(*state_, panel_slot::right, "State Machine");

    render_header_();
    render_params_();
    render_states_();

    ImGui::SeparatorText("From any");
    render_rules_(state_->fsm.data.any_transitions, "any");

    end_panel(*state_, panel_slot::right);
}

auto fsm_panel::render_header_() -> void {
    auto& data = state_->fsm.data;

    ImGui::TextDisabled("%s", state_->fsm.source.str().c_str());

    if (ImGui::Button("Save")) {
        static_cast<void>(service_->save());
    }
    ImGui::SameLine();
    if (state_->fsm.has_unsaved_changes) {
        ImGui::TextDisabled("modified");
    } else {
        ImGui::TextDisabled("saved");
    }

    auto rig = data.rig;
    field_label("Rig");
    imgui_input_text_string("##fsm_rig", rig);
    if (rig != data.rig) {
        begin_edit_();
        data.rig = rig;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        commit_edit_();
    }

    field_label("Entry");
    if (state_combo_("##fsm_entry", data.entry_state)) {
        commit_edit_();
    }
}

auto fsm_panel::render_params_() -> void {
    auto& data = state_->fsm.data;

    ImGui::SeparatorText("Parameters");

    std::optional<std::size_t> to_remove;

    for (std::size_t i = 0; i < data.params.size(); ++i) {
        auto& param = data.params[i];
        ImGui::PushID(static_cast<int32>(i));

        auto name = param.name;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        imgui_input_text_string("##name", name);
        if (name != param.name) {
            begin_edit_();
            param.name = name;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        ImGui::SameLine();
        auto type = static_cast<int32>(param.type);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        if (ImGui::Combo("##type", &type, param_type_names.data(),
                         static_cast<int32>(param_type_names.size()))) {
            begin_edit_();
            param.type = static_cast<asset::voxf_param_type>(type);
            commit_edit_();
        }

        if (param.type != asset::voxf_param_type::trigger) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            if (param.type == asset::voxf_param_type::boolean) {
                bool on = param.value != 0.0F;
                if (ImGui::Checkbox("##value", &on)) {
                    begin_edit_();
                    param.value = on ? 1.0F : 0.0F;
                    commit_edit_();
                }
            } else if (ImGui::DragFloat("##value", &param.value, 0.01f)) {
                begin_edit_();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit_edit_();
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            to_remove = i;
        }

        ImGui::PopID();
    }

    if (to_remove.has_value()) {
        begin_edit_();
        data.params.erase(data.params.begin() + static_cast<std::ptrdiff_t>(*to_remove));
        commit_edit_();
    }

    if (ImGui::SmallButton("Add##param")) {
        begin_edit_();
        data.params.push_back(asset::voxf_param{.name = "parameter"});
        commit_edit_();
    }
}

auto fsm_panel::state_combo_(
    const char* label, std::string& target
) -> bool {
    const auto& data = state_->fsm.data;

    bool changed = false;
    if (ImGui::BeginCombo(label, target.c_str())) {
        for (const auto& state : data.states) {
            if (ImGui::Selectable(state.name.c_str(), state.name == target)) {
                begin_edit_();
                target  = state.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    return changed;
}

auto fsm_panel::render_states_() -> void {
    auto& data = state_->fsm.data;

    ImGui::SeparatorText("States");

    for (std::size_t i = 0; i < data.states.size(); ++i) {
        render_state_(i);
    }

    if (ImGui::SmallButton("Add##state")) {
        begin_edit_();
        data.states.push_back(asset::voxf_state{.name = std::format("state_{}", data.states.size())});
        commit_edit_();
    }
}

auto fsm_panel::render_state_(
    std::size_t index
) -> void {
    auto& data  = state_->fsm.data;
    auto& state = data.states[index];

    ImGui::PushID(static_cast<int32>(index));

    const auto header = std::format(
        "{}{}###state", state.name, state.name == data.entry_state ? "  (entry)" : ""
    );

    if (ImGui::CollapsingHeader(header.c_str())) {
        state_->fsm.selected_state = state.name;

        auto name = state.name;
        field_label("Name");
        imgui_input_text_string("##state_name", name);
        if (name != state.name) {
            begin_edit_();
            state.name = name;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (editing_ && index < before_.states.size()) {
                const auto& old_name = before_.states[index].name;
                for (auto& other : data.states) {
                    for (auto& rule : other.transitions) {
                        if (rule.target_state == old_name) {
                            rule.target_state = state.name;
                        }
                    }
                }
                for (auto& rule : data.any_transitions) {
                    if (rule.target_state == old_name) {
                        rule.target_state = state.name;
                    }
                }
                if (data.entry_state == old_name) {
                    data.entry_state = state.name;
                }
            }
            commit_edit_();
        }

        auto clip = std::string{state.clip.str()};
        field_label("Clip");
        imgui_input_text_string("##state_clip", clip);
        if (clip != state.clip.str()) {
            begin_edit_();
            state.clip = asset::asset_ref{clip};
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        auto playback = static_cast<int32>(state.loop_mode);
        field_label("Playback");
        if (ImGui::Combo("##state_playback", &playback, playback_names.data(),
                         static_cast<int32>(playback_names.size()))) {
            begin_edit_();
            state.loop_mode = static_cast<asset::animation_loop_mode>(playback);
            commit_edit_();
        }

        field_label("Rate");
        if (ImGui::DragFloat("##state_rate", &state.rate, 0.01f, 0.01f, 8.0f)) {
            begin_edit_();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        field_label("Fade in");
        if (ImGui::DragFloat("##state_fade_in", &state.fade_in.duration, 0.01f, 0.0f, 4.0f)) {
            begin_edit_();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        field_label("Fade out");
        if (ImGui::DragFloat("##state_fade_out", &state.fade_out.duration, 0.01f, 0.0f, 4.0f)) {
            begin_edit_();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        render_rules_(state.transitions, "out");
        render_incoming_(state.name);

        if (ImGui::SmallButton("Delete")) {
            begin_edit_();
            data.states.erase(data.states.begin() + static_cast<std::ptrdiff_t>(index));
            commit_edit_();
            ImGui::PopID();
            return;
        }
    }

    ImGui::PopID();
}

auto fsm_panel::render_rules_(
    std::vector<asset::animation_fsm::transition_rule>& rules, const char* id
) -> void {
    ImGui::PushID(id);

    for (std::size_t i = 0; i < rules.size(); ++i) {
        render_rule_(rules, i);
    }

    if (ImGui::SmallButton("Add transition")) {
        begin_edit_();
        rules.push_back({});
        commit_edit_();
    }

    ImGui::PopID();
}

auto fsm_panel::render_rule_(
    std::vector<asset::animation_fsm::transition_rule>& rules, std::size_t index
) -> void {
    auto& data = state_->fsm.data;
    auto& rule = rules[index];

    ImGui::PushID(static_cast<int32>(index));

    const auto header = std::format("-> {}###rule", rule_summary(data, rule));

    if (ImGui::TreeNode(header.c_str())) {
        field_label("To");
        if (state_combo_("##rule_target", rule.target_state)) {
            commit_edit_();
        }

        auto trigger = rule.trigger_name;
        field_label("On");
        imgui_input_text_string("##rule_trigger", trigger);
        if (trigger != rule.trigger_name) {
            begin_edit_();
            rule.trigger_name = trigger;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        field_label("Blend");
        if (ImGui::DragFloat("##rule_blend", &rule.blend.duration, 0.01f, 0.0f, 4.0f)) {
            begin_edit_();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        bool wait_end = rule.wait_until_end;
        if (ImGui::Checkbox("wait end", &wait_end)) {
            begin_edit_();
            rule.wait_until_end = wait_end;
            commit_edit_();
        }
        ImGui::SameLine();
        bool wait_blend = rule.wait_until_blend;
        if (ImGui::Checkbox("wait blend", &wait_blend)) {
            begin_edit_();
            rule.wait_until_blend = wait_blend;
            commit_edit_();
        }

        std::optional<std::size_t> condition_to_remove;

        for (std::size_t i = 0; i < rule.conditions.size(); ++i) {
            auto& condition = rule.conditions[i];
            ImGui::PushID(static_cast<int32>(i));

            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
            if (ImGui::BeginCombo("##param", condition.parameter.c_str())) {
                for (const auto& param : data.params) {
                    if (param.type == asset::voxf_param_type::trigger) {
                        continue;
                    }
                    if (ImGui::Selectable(param.name.c_str(), param.name == condition.parameter)) {
                        begin_edit_();
                        condition.parameter = param.name;
                        commit_edit_();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            auto compare = static_cast<int32>(condition.compare);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
            if (ImGui::Combo("##compare", &compare, compare_names.data(),
                             static_cast<int32>(compare_names.size()))) {
                begin_edit_();
                condition.compare = static_cast<asset::fsm_compare>(compare);
                commit_edit_();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            if (ImGui::DragFloat("##value", &condition.value, 0.01f)) {
                begin_edit_();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit_edit_();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                condition_to_remove = i;
            }

            ImGui::PopID();
        }

        if (condition_to_remove.has_value()) {
            begin_edit_();
            rule.conditions.erase(
                rule.conditions.begin() + static_cast<std::ptrdiff_t>(*condition_to_remove)
            );
            commit_edit_();
        }

        if (ImGui::SmallButton("Add condition")) {
            begin_edit_();
            rule.conditions.push_back({});
            commit_edit_();
        }

        ImGui::SameLine();
        const bool remove_rule = ImGui::SmallButton("Delete");

        ImGui::TreePop();

        if (remove_rule) {
            begin_edit_();
            rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
            commit_edit_();
        }
    }

    ImGui::PopID();
}

auto fsm_panel::render_incoming_(
    const std::string& state_name
) -> void {
    const auto sources = state_->fsm.incoming(state_name);

    ImGui::TextDisabled("incoming");
    ImGui::SameLine(label_width());

    if (sources.empty()) {
        ImGui::TextDisabled(
            "%s", state_name == state_->fsm.data.entry_state ? "entry" : "nothing leads here"
        );
        return;
    }

    std::string text;
    for (const auto& source : sources) {
        if (!text.empty()) {
            text += ", ";
        }
        text += source;
    }

    ImGui::TextUnformatted(text.c_str());
}

}  // namespace vw::sculptor
