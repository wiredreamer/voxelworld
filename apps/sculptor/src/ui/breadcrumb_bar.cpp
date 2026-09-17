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
    engine_type& eng, app_state& st, clip_service& clip_svc
)
    : engine_(&eng), state_(&st), clip_service_(&clip_svc) {}

auto breadcrumb_bar::render(
    float /*delta_time*/
) -> void {
    if (state_->file.filename.empty() && state_->ctx.in_prefab()) {
        return;
    }

    begin_panel(*state_, panel_slot::left, "Breadcrumbs", nullptr, false);

    const auto& stack = state_->ctx.stack;

    // Корень — сам документ, и он кликабелен, даже когда стек пуст: так видно,
    // что крошки это путь, а не заголовок.
    const std::string& document =
        state_->file.filename.empty() ? std::string{"untitled"} : state_->file.filename;

    if (ImGui::Button(document.c_str())) {
        leave_to_(0);
    }

    for (std::size_t i = 0; i < stack.size(); ++i) {
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();

        const auto label = std::format("{}##crumb{}", label_of_(stack[i]), i);
        if (ImGui::Button(label.c_str())) {
            leave_to_(i + 1);
        }
    }

    end_panel(*state_, panel_slot::left);
}

auto breadcrumb_bar::leave_to_(
    std::size_t depth
) -> void {
    const auto& stack = state_->ctx.stack;

    bool drops_clip = false;
    for (std::size_t i = depth; i < stack.size(); ++i) {
        drops_clip = drops_clip || stack[i].kind == edit_kind::clip;
    }

    // Выход из клипа — это не только снятие контекста: слои останавливаются, а
    // поза возвращается к той, что лежит в префабе. Сам клип при этом остаётся
    // открытым, и вернуться к нему можно из меню.
    if (drops_clip) {
        clip_service_->exit_animation_mode();
        state_->ui.show_timeline = false;
        return;
    }

    state_->ctx.leave_to(depth);
}

auto breadcrumb_bar::label_of_(
    const edit_context& ctx
) const -> std::string {
    if (ctx.kind == edit_kind::clip) {
        const auto& clip_name = state_->anim.selected_clip_name;
        return clip_name.empty() ? std::string{"animation"} : std::format("{}.voxa", clip_name);
    }

    if (ctx.kind == edit_kind::fsm) {
        return state_->fsm.is_open() ? std::string{state_->fsm.source.stem()} + ".voxf"
                                     : std::string{"machine"};
    }

    if (ctx.kind != edit_kind::model) {
        return ctx.node_name;
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
