module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {
namespace {

auto to_imvec4(color clr) -> ImVec4 {
    return {
        static_cast<float>(clr.r()) / 255.0f,
        static_cast<float>(clr.g()) / 255.0f,
        static_cast<float>(clr.b()) / 255.0f,
        1.0f
    };
}

constexpr int32 swatches_per_row = 6;

}  // namespace


block_palette_panel::block_palette_panel(
    engine_type& eng, app_state& st
)
    : engine_(&eng), state_(&st) {}

auto block_palette_panel::swatch_(
    const block_type& block, int32 index_in_row
) -> void {
    // Перенос ставится перед образцом, а не после: иначе последний в ряду
    // утащил бы на свою строку то, что идёт за палитрой.
    if (index_in_row % swatches_per_row != 0) {
        ImGui::SameLine(0, 0);
    }

    ImGui::PushID(static_cast<int>(block.slot.value));

    constexpr ImGuiColorEditFlags btn_flags =  //
        ImGuiColorEditFlags_NoAlpha |          //
        ImGuiColorEditFlags_NoPicker |         //
        ImGuiColorEditFlags_NoBorder;

    if (ImGui::ColorButton(
            "##block", to_imvec4(block.material.clr), btn_flags, ImVec2(30.0f, 30.0f)
        )) {
        state_->tool.selected_block = block.id;
    }
    ImGui::SetItemTooltip("%.*s", static_cast<int>(block.name.size()), block.name.data());

    ImGui::PopID();
}

auto block_palette_panel::render(
    [[maybe_unused]] float delta_time
) -> void {
    const block_registry& registry = engine_->get_block_registry();
    const block_category shown     = selected_model_category(*engine_, *state_);

    // Кисть помнится на набор, и пишется она каждый кадр: так запоминается
    // последний выбор, чем бы он ни был сделан — панелью, пипеткой или
    // открытием файла.
    state_->tool.brush_of_set[state_->tool.selected_block.category().value] =
        state_->tool.selected_block;

    // Кисть, оставшаяся от модели другого набора, в текущую не ложится вовсе —
    // и уронила бы движок на первом же мазке. Поэтому она переезжает вместе с
    // панелью, а не проверяется на каждом инструменте по отдельности.
    if (state_->tool.selected_block.category() != shown) {
        state_->tool.selected_block = state_->tool.brush_for(shown, registry);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 window_pos       = ImVec2(
        viewport->WorkPos.x + 10,
        viewport->WorkPos.y + viewport->WorkSize.y  //
            - state_->ui.left_bottom_voffset        //
            - 10
    );
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));

    ImGuiWindowFlags window_flags =         //
        ImGuiWindowFlags_NoSavedSettings |  //
        ImGuiWindowFlags_NoMove |           //
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Block Palette", nullptr, window_flags);

    const block_type& selected = registry.get(state_->tool.selected_block);
    const color selected_color = selected.material.clr;

    ImGui::ColorButton(
        "##current_block",
        to_imvec4(selected_color),
        ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker,
        ImVec2(ImGui::GetContentRegionAvail().x, 40.0f)
    );
    ImGui::Text("%.*s", static_cast<int>(selected.name.size()), selected.name.data());
    ImGui::Text(
        "#%02X%02X%02X  %u:%u",
        selected_color.r(),
        selected_color.g(),
        selected_color.b(),
        selected.id.category().value,
        selected.id.index()
    );

    if (selected.material.emission != 0 || selected.material.glow != 0) {
        ImGui::Text(
            "emits %u, glows %u", selected.material.emission, selected.material.glow
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const block_set* set = registry.set_of(shown);

    if (set != nullptr) {
        for (const block_group& group : set->groups) {
            ImGui::SeparatorText(std::string{group.name}.c_str());

            // Нулевой отступ только вокруг образцов: рампа обязана читаться
            // сплошной полосой, а заголовки от неё — отделяться.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            int32 in_row = 0;
            for (uint8 offset = 0; offset < group.count; ++offset) {
                const block_type& block = registry.get(group.at(offset));
                if (block.slot == missing_block_slot) {
                    continue;
                }
                swatch_(block, in_row);
                ++in_row;
            }
            ImGui::PopStyleVar();
        }
    } else {
        // Набор, о разделах которого каталог не знает: показать всё равно есть
        // что — списком, — а придумывать за него разбиение панель не станет.
        ImGui::SeparatorText("blocks");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        int32 in_row = 0;
        for (const block_type& block : registry.all()) {
            if (block.id == blocks::air || block.id.category() != shown) {
                continue;
            }
            swatch_(block, in_row);
            ++in_row;
        }
        ImGui::PopStyleVar();
    }

    state_->ui.left_bottom_voffset += ImGui::GetWindowHeight() + 10.f;

    ImGui::End();
}


}  // namespace vw::sculptor
