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
constexpr std::array<const char*, 2> truth_names{"false", "true"};

constexpr float32 panel_width     = 440.0f;
constexpr float32 run_panel_width = 320.0f;
constexpr const char* nothing     = "(none)";

auto label_width() -> float32 {
    return ImGui::GetFontSize() * 7.5f;
}

auto field_label(const char* text) -> void {
    const float32 field_start = ImGui::GetCursorPosX() + label_width();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(field_start);
    ImGui::SetNextItemWidth(-1.0f);
}

auto gap() -> void {
    ImGui::Dummy(ImVec2{0.0f, ImGui::GetFontSize() * 0.45f});
}

auto text_field(const char* id, std::string& value) -> void {
    std::array<char, 128> buffer{};
    value.copy(buffer.data(), buffer.size() - 1);

    if (ImGui::InputText(id, buffer.data(), buffer.size())) {
        value = std::string{buffer.data()};
    }
}

auto type_of(const asset::voxf_data& data, std::string_view parameter) -> asset::voxf_param_type {
    const auto it = std::ranges::find(data.params, parameter, &asset::voxf_param::name);
    return it == data.params.end() ? asset::voxf_param_type::real : it->type;
}

auto condition_text(const asset::voxf_data& data, const asset::fsm_condition& condition)
    -> std::string {
    const bool as_bool =
        type_of(data, condition.parameter) == asset::voxf_param_type::boolean;

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

    begin_panel(*state_, panel_slot::right, "State Machine", nullptr, true, panel_width);

    render_header_();
    gap();
    render_params_();
    gap();
    render_states_();
    gap();

    ImGui::SeparatorText("From any state");
    render_rules_(state_->fsm.data.any_transitions, "any");
    gap();

    render_run_();

    end_panel(*state_, panel_slot::right);

    render_run_window_();
}

auto fsm_panel::render_header_() -> void {
    auto& data = state_->fsm.data;

    ImGui::TextDisabled("%s", state_->fsm.source.str().c_str());

    auto rig = data.rig;
    field_label("Rig");
    text_field("##fsm_rig", rig);
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

auto fsm_panel::render_run_() -> void {
    ImGui::SeparatorText("Run");

    if (service_->run_status().running) {
        if (ImGui::Button("Stop")) {
            service_->stop();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("running, edits apply as they are made");
        return;
    }

    if (ImGui::Button("Run")) {
        const auto started = service_->run();
        run_error_         = started ? std::string{} : started.error();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("every machine of the prefab, from its entry state");

    if (!run_error_.empty()) {
        ImGui::TextWrapped("%s", run_error_.c_str());
    }
}

auto fsm_panel::render_run_window_() -> void {
    const machine_run_status status = service_->run_status();
    if (!status.running) {
        return;
    }

    begin_panel(*state_, panel_slot::left, "Machine Run", nullptr, true, run_panel_width);

    for (std::size_t index = 0; index < status.layers.size(); ++index) {
        const machine_layer_status& layer = status.layers[index];

        ImGui::Text(
            "layer %d  %s", static_cast<int32>(index), std::string{layer.machine.stem()}.c_str()
        );
        ImGui::Indent();
        ImGui::Text("%s", layer.state.c_str());
        if (!layer.clip.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s  %.2f s", layer.clip.c_str(), layer.time);
        }
        ImGui::Unindent();
    }

    machine_input input;

    if (!status.parameters.empty()) {
        gap();
        ImGui::SeparatorText("Parameters");
    }
    for (const machine_parameter& parameter : status.parameters) {
        ImGui::PushID(parameter.name.c_str());
        field_label(parameter.name.c_str());

        switch (parameter.type) {
            case asset::voxf_param_type::boolean: {
                bool flag = parameter.value != 0.0F;
                if (ImGui::Checkbox("##value", &flag)) {
                    input.values.emplace_back(parameter.name, flag ? 1.0F : 0.0F);
                }
                break;
            }
            case asset::voxf_param_type::integer: {
                auto whole = static_cast<int32>(std::lround(parameter.value));
                if (ImGui::InputInt("##value", &whole)) {
                    input.values.emplace_back(parameter.name, static_cast<float32>(whole));
                }
                break;
            }
            case asset::voxf_param_type::real:
            case asset::voxf_param_type::trigger: {
                float32 value = parameter.value;
                if (ImGui::DragFloat("##value", &value, 0.01f)) {
                    input.values.emplace_back(parameter.name, value);
                }
                break;
            }
        }

        ImGui::PopID();
    }

    if (!status.triggers.empty()) {
        gap();
        ImGui::SeparatorText("Triggers");
    }
    for (const std::string& trigger : status.triggers) {
        if (ImGui::Button(trigger.c_str())) {
            input.triggers.push_back(trigger);
        }
        ImGui::SameLine();
    }
    if (!status.triggers.empty()) {
        ImGui::NewLine();
    }

    if (!input.values.empty() || !input.triggers.empty()) {
        const auto driven = service_->drive(input);
        run_error_        = driven ? std::string{} : driven.error();
    }

    if (!status.note.empty() || !run_error_.empty()) {
        gap();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(status.note.empty() ? run_error_.c_str() : status.note.c_str());
        ImGui::PopTextWrapPos();
    }

    gap();
    if (ImGui::Button("Stop")) {
        service_->stop();
    }

    end_panel(*state_, panel_slot::left);
}

auto fsm_panel::render_params_() -> void {
    auto& data = state_->fsm.data;

    ImGui::SeparatorText("Parameters");

    std::optional<std::size_t> to_remove;

    for (std::size_t i = 0; i < data.params.size(); ++i) {
        auto& param = data.params[i];
        ImGui::PushID(static_cast<int32>(i));

        auto name = param.name;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        text_field("##name", name);
        if (name != param.name) {
            begin_edit_();
            param.name = name;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_edit_();
        }

        ImGui::SameLine();
        auto type = static_cast<int32>(param.type);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.5f);
        if (ImGui::Combo("##type", &type, param_type_names.data(),
                         static_cast<int32>(param_type_names.size()))) {
            begin_edit_();
            param.type = static_cast<asset::voxf_param_type>(type);
            commit_edit_();
        }

        ImGui::SameLine();
        if (value_field_("##value", param.type, param.value)) {
            commit_edit_();
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
                        ImGui::GetFontSize() * 1.6f);
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

    if (ImGui::SmallButton("Add parameter")) {
        begin_edit_();
        data.params.push_back(asset::voxf_param{.name = "parameter"});
        commit_edit_();
    }
}

auto fsm_panel::value_field_(
    const char* id, asset::voxf_param_type type, float32& value
) -> bool {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);

    switch (type) {
        case asset::voxf_param_type::trigger:
            ImGui::Dummy(ImVec2{ImGui::GetFontSize() * 5.0f, ImGui::GetFrameHeight()});
            return false;

        case asset::voxf_param_type::boolean: {
            auto truth = value != 0.0F ? 1 : 0;
            if (ImGui::Combo(id, &truth, truth_names.data(), static_cast<int32>(truth_names.size()))) {
                begin_edit_();
                value = truth != 0 ? 1.0F : 0.0F;
                return true;
            }
            return false;
        }

        case asset::voxf_param_type::integer: {
            auto whole = static_cast<int32>(std::lround(value));
            if (ImGui::DragInt(id, &whole, 0.1f)) {
                begin_edit_();
                value = static_cast<float32>(whole);
            }
            return ImGui::IsItemDeactivatedAfterEdit();
        }

        case asset::voxf_param_type::real:
            if (ImGui::DragFloat(id, &value, 0.01f)) {
                begin_edit_();
            }
            return ImGui::IsItemDeactivatedAfterEdit();
    }
    return false;
}

auto fsm_panel::state_combo_(
    const char* label, std::string& target
) -> bool {
    const auto& data = state_->fsm.data;

    bool changed = false;
    if (ImGui::BeginCombo(label, target.empty() ? nothing : target.c_str())) {
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

auto fsm_panel::choice_combo_(
    const char* label, std::string& chosen, const std::vector<std::string>& choices
) -> bool {
    bool changed = false;
    if (ImGui::BeginCombo(label, chosen.empty() ? nothing : chosen.c_str())) {
        if (ImGui::Selectable(nothing, chosen.empty())) {
            begin_edit_();
            chosen.clear();
            changed = true;
        }
        for (const std::string& choice : choices) {
            if (ImGui::Selectable(choice.c_str(), choice == chosen)) {
                begin_edit_();
                chosen  = choice;
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

    gap();
    if (ImGui::SmallButton("Add state")) {
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

    if (!ImGui::CollapsingHeader(header.c_str())) {
        ImGui::PopID();
        return;
    }

    state_->fsm.selected_state = state.name;

    ImGui::Indent();
    gap();

    auto name = state.name;
    field_label("Name");
    text_field("##state_name", name);
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
    if (choice_combo_("##state_clip", clip, service_->clip_choices())) {
        state.clip = asset::asset_ref{clip};
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

    gap();
    ImGui::TextDisabled("Transitions, checked in this order");
    render_rules_(state.transitions, "out");

    gap();
    render_incoming_(state.name);

    gap();
    const bool remove_state = ImGui::SmallButton("Delete state");

    gap();
    ImGui::Unindent();
    ImGui::PopID();

    if (remove_state) {
        begin_edit_();
        data.states.erase(data.states.begin() + static_cast<std::ptrdiff_t>(index));
        commit_edit_();
    }
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

    if (!ImGui::TreeNode(header.c_str())) {
        ImGui::PopID();
        return;
    }

    gap();

    field_label("To");
    if (state_combo_("##rule_target", rule.target_state)) {
        commit_edit_();
    }

    std::vector<std::string> triggers;
    for (const auto& param : data.params) {
        if (param.type == asset::voxf_param_type::trigger) {
            triggers.push_back(param.name);
        }
    }

    field_label("On trigger");
    if (choice_combo_("##rule_trigger", rule.trigger_name, triggers)) {
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
    if (ImGui::Checkbox("wait for the clip to end", &wait_end)) {
        begin_edit_();
        rule.wait_until_end = wait_end;
        commit_edit_();
    }
    bool wait_blend = rule.wait_until_blend;
    if (ImGui::Checkbox("wait for the blend to end", &wait_blend)) {
        begin_edit_();
        rule.wait_until_blend = wait_blend;
        commit_edit_();
    }

    gap();
    ImGui::TextDisabled("Conditions, all must hold");

    std::optional<std::size_t> condition_to_remove;

    for (std::size_t i = 0; i < rule.conditions.size(); ++i) {
        auto& condition = rule.conditions[i];
        ImGui::PushID(static_cast<int32>(i));

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        if (ImGui::BeginCombo(
                "##param", condition.parameter.empty() ? nothing : condition.parameter.c_str()
            )) {
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
        if (value_field_("##value", type_of(data, condition.parameter), condition.value)) {
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

    gap();
    const bool remove_rule = ImGui::SmallButton("Delete transition");
    gap();

    ImGui::TreePop();
    ImGui::PopID();

    if (remove_rule) {
        begin_edit_();
        rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
        commit_edit_();
    }
}

auto fsm_panel::render_incoming_(
    const std::string& state_name
) -> void {
    const auto sources = state_->fsm.incoming(state_name);

    const float32 field_start = ImGui::GetCursorPosX() + label_width();

    ImGui::TextDisabled("Reached from");
    ImGui::SameLine(field_start);

    if (sources.empty()) {
        ImGui::TextDisabled(
            "%s", state_name == state_->fsm.data.entry_state ? "the entry" : "nowhere"
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
