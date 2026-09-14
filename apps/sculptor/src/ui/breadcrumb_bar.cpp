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

breadcrumb_bar::breadcrumb_bar(
    engine_type& eng, app_state& st
)
    : engine_(&eng), state_(&st) {}

auto breadcrumb_bar::render(
    float /*delta_time*/
) -> void {
    if (state_->file.filename.empty() && state_->ctx.in_prefab()) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos =
        ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + state_->ui.left_top_voffset + 10);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoCollapse |          //
        ImGuiWindowFlags_NoTitleBar |          //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Breadcrumbs", nullptr, window_flags);

    const auto& stack = state_->ctx.stack;

    // Корень — сам документ, и он кликабелен, даже когда стек пуст: так видно,
    // что крошки это путь, а не заголовок.
    const std::string& document =
        state_->file.filename.empty() ? std::string{"untitled"} : state_->file.filename;

    if (ImGui::Button(document.c_str())) {
        state_->ctx.leave_to(0);
    }

    for (std::size_t i = 0; i < stack.size(); ++i) {
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();

        const auto label = std::format("{}##crumb{}", label_of_(stack[i]), i);
        if (ImGui::Button(label.c_str())) {
            state_->ctx.leave_to(i + 1);
        }
    }

    state_->ui.left_top_voffset += ImGui::GetWindowHeight() + 10.f;

    ImGui::End();
}

auto breadcrumb_bar::label_of_(
    const edit_context& ctx
) const -> std::string {
    if (ctx.kind != edit_kind::model) {
        return std::format("{}{}", ctx.ref.stem(), ctx.ref.extension());
    }

    // Имя файла объёма берётся из мира, а не из контекста: у нового узла ссылки
    // ещё нет, а после первой записи она появляется, и крошка обязана это
    // показать, не заставляя выходить и заходить снова.
    const auto it = state_->scene.name_to_entity.find(ctx.node_name);
    if (it != state_->scene.name_to_entity.end()) {
        const auto& world = engine_->get_world();
        if (world.has<ecs::model_component>(it->second)) {
            const auto& ref = world.get<ecs::model_component>(it->second).get_source();
            if (!ref.empty()) {
                return std::format("{}{}", ref.stem(), ref.extension());
            }
        }
    }

    return ctx.node_name;
}

}  // namespace vw::sculptor
