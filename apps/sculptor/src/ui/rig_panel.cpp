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

rig_panel::rig_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto rig_panel::render(
    float /*delta_time*/
) -> void {
    const auto root_it = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root_it == state_->scene.name_to_entity.end()) {
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

    bool still_open = true;
    ImGui::Begin("Rig", &still_open, window_flags);
    if (!still_open) {
        state_->ui.show_rig = false;
    }

    render_name_(root_it->second);

    ImGui::Separator();
    ImGui::Spacing();

    render_targets_(root_it->second);

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    ImGui::End();
}

auto rig_panel::render_name_(
    ecs::entity root
) -> void {
    auto& world = engine_->get_world();

    static const std::string no_rig;
    const auto& current = world.has<ecs::rig_component>(root) ?
        world.get<ecs::rig_component>(root).get_name() :
        no_rig;

    imgui_input_text_string("Rig", rig_input_);

    // Имя уезжает в операцию по окончании правки, а не по каждой букве: иначе
    // стек undo забьётся посимвольной историей набора.
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (rig_input_ != current) {
            op_manager_->execute(
                std::make_unique<set_rig_operation>(
                    *engine_, *state_, set_rig_params{.rig_name = rig_input_}
                )
            );
        }
    } else if (!ImGui::IsItemActive()) {
        rig_input_ = current;
    }

    if (current.empty()) {
        ImGui::TextDisabled("no rig: clips are not checked");
    }
}

auto rig_panel::render_targets_(
    ecs::entity root
) -> void {
    auto& anim_sys     = engine_->get_world().system<ecs::animation_system>();
    const auto targets = anim_sys.collect_targets(root);

    ImGui::Text("Targets: %d", static_cast<int>(targets.size()));

    if (targets.empty()) {
        ImGui::TextDisabled("none: no node carries an animation target");
        return;
    }

    for (const auto& name : targets) {
        ImGui::BulletText("%s", name.c_str());
    }
}

}  // namespace vw::sculptor
