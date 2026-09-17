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

constexpr const char* node_payload = "VW_NODE";

// Кромка строки, бросок на которую ставит узел соседом, — доля высоты с каждой
// стороны. Середина — самое частое намерение, и ей отдана половина строки.
constexpr float32 drop_edge = 0.25f;

}  // namespace

entity_tree_panel::entity_tree_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager,
    asset::model_library& library
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , creation_modal_(eng, st, op_manager, library)
    , deletion_modal_(eng, st, op_manager) {}

auto entity_tree_panel::render(
    float delta_time
) -> void {
    begin_panel(*state_, panel_slot::right, "Entity Tree");

    // Состав правится только в самом префабе. Внутри объёма дерево открыто ради
    // глаз — спрятать соседние части, не выходя из правки.
    if (state_->ctx.in_prefab()) {
        // Родителя выбирают в самом окне, так что добавлять можно и без выделения.
        if (ImGui::Button("Add")) {
            creation_modal_.open();
        }

        ImGui::SameLine();

        const bool can_remove = !state_->scene.selected_name.empty() &&
            state_->scene.name_to_entity.contains(state_->scene.selected_name);
        ImGui::BeginDisabled(!can_remove);
        if (ImGui::Button("Remove")) {
            deletion_modal_.open(state_->scene.selected_name);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    if (!state_->scene.root_name.empty()) {
        const auto preview_entities = state_->sockets.get_preview_entities();
        render_entity_node(state_->scene.root_name, preview_entities, false);
    }

    if (pending_move_.has_value()) {
        op_manager_->execute(
            std::make_unique<move_entity_operation>(*engine_, *state_, *pending_move_)
        );
        pending_move_.reset();
    }

    creation_modal_.render(delta_time);
    deletion_modal_.render(delta_time);

    ImGui::Dummy({200.0f, 0.0f});

    end_panel(*state_, panel_slot::right);
}

// Провалившись в .voxm, правишь именно тот узел, через который вошёл, и уехать
// из него мимо крошек нельзя. В клипе выбор, наоборот, нужен: им говорят, чью
// дорожку правят.
auto entity_tree_panel::select_(const std::string& name) const -> void {
    if (!state_->ctx.allows_node_select()) {
        return;
    }
    state_->scene.selected_name = name;
}

auto entity_tree_panel::render_entity_node(
    const std::string& name, const std::unordered_set<ecs::entity>& preview_entities,
    bool parent_hidden
) -> void {
    const auto found = state_->scene.name_to_entity.find(name);
    if (name.empty() || found == state_->scene.name_to_entity.end()) {
        return;
    }

    const auto ent = found->second;
    auto& world    = engine_->get_world();

    bool has_children = false;
    if (world.has<ecs::hierarchy_component>(ent)) {
        const auto& hierarchy_comp = world.get<ecs::hierarchy_component>(ent);
        has_children = std::ranges::any_of(hierarchy_comp.get_children(), [&](auto child) {
            return !preview_entities.contains(child);
        });
    }

    const bool hidden = parent_hidden || state_->scene.hidden_nodes.contains(name);

    // Внутри объёма подсвечен узел из крошек: выделение туда не ходит, а
    // показать, чьи соседи вокруг, всё равно нужно.
    const bool is_selected = state_->edited_node() == name;
    bool is_open           = false;

    render_visibility_toggle_(name);
    ImGui::SameLine();

    ImGuiTreeNodeFlags node_flags =           //
        ImGuiTreeNodeFlags_OpenOnArrow |      //
        ImGuiTreeNodeFlags_OpenOnDoubleClick | //
        ImGuiTreeNodeFlags_DefaultOpen |      //
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (is_selected) {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Скрытый узел — и скрытый вслед за предком — читается приглушённым, чтобы по
    // дереву было видно, почему части нет на экране.
    if (hidden) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }

    const auto node_id = std::format("{}##{}.{}", name, ent.index, ent.generation);
    if (has_children) {
        is_open = ImGui::TreeNodeEx(node_id.c_str(), node_flags);
    } else if (ImGui::Selectable(node_id.c_str(), is_selected)) {
        select_(name);
    }

    if (hidden) {
        ImGui::PopStyleColor();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        select_(name);
    }

    if (state_->ctx.in_prefab()) {
        render_drag_and_drop_(name);

        const auto context_menu_id =
            std::format("EntityContextMenu_{}_{}", ent.index, ent.generation);
        if (ImGui::BeginPopupContextItem(context_menu_id.c_str())) {
            state_->scene.selected_name = name;
            if (ImGui::MenuItem("Create child")) {
                creation_modal_.open();
            }
            if (!has_children && ImGui::MenuItem("Delete")) {
                deletion_modal_.open(name);
            }
            ImGui::EndPopup();
        }
    }

    if (is_open && has_children) {
        for (const auto child : world.get<ecs::hierarchy_component>(ent).get_children()) {
            if (preview_entities.contains(child)) {
                continue;
            }
            const auto child_name = state_->scene.entity_to_name.find(child);
            if (child_name != state_->scene.entity_to_name.end()) {
                render_entity_node(child_name->second, preview_entities, hidden);
            }
        }

        ImGui::TreePop();
    }
}

auto entity_tree_panel::render_visibility_toggle_(
    const std::string& name
) -> void {
    // Объём, в который провалились, не прячется: править невидимое нечем.
    const bool locked =
        state_->ctx.kind() == edit_kind::model && state_->ctx.node_name() == name;

    bool shown = !state_->scene.hidden_nodes.contains(name);

    ImGui::PushID(name.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 0.f));
    ImGui::BeginDisabled(locked);

    if (ImGui::Checkbox("##shown", &shown)) {
        if (shown) {
            state_->scene.hidden_nodes.erase(name);
        } else {
            state_->scene.hidden_nodes.insert(name);
        }
    }
    ImGui::SetItemTooltip("%s", shown ? "Hide" : "Show");

    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

auto entity_tree_panel::render_drag_and_drop_(
    const std::string& name
) -> void {
    // Корень префаба один и остаётся корнем: переносить его некуда.
    if (name != state_->scene.root_name && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(node_payload, name.c_str(), name.size() + 1);
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }

    if (!ImGui::BeginDragDropTarget()) {
        return;
    }

    const ImVec2 min     = ImGui::GetItemRectMin();
    const ImVec2 max     = ImGui::GetItemRectMax();
    const float32 height = max.y - min.y;
    const float32 y      = ImGui::GetMousePos().y - min.y;

    // У корня соседей нет, и любой бросок на него — внутрь.
    auto place = drop_place::inside;
    if (name != state_->scene.root_name) {
        if (y < height * drop_edge) {
            place = drop_place::before;
        } else if (y > height * (1.f - drop_edge)) {
            place = drop_place::after;
        }
    }

    constexpr ImGuiDragDropFlags accept_flags =  //
        ImGuiDragDropFlags_AcceptBeforeDelivery |  //
        ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(node_payload, accept_flags)) {
        const std::string dragged{static_cast<const char*>(payload->Data)};

        // Недопустимый бросок ничего не рисует: подсказка обещает ровно то, что
        // случится, если отпустить.
        if (auto move = plan_move_(dragged, name, place)) {
            auto* draw_list   = ImGui::GetWindowDrawList();
            const auto colour = ImGui::GetColorU32(ImGuiCol_DragDropTarget);

            switch (place) {
                case drop_place::before:
                    draw_list->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), colour, 2.f);
                    break;
                case drop_place::after:
                    draw_list->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), colour, 2.f);
                    break;
                case drop_place::inside: draw_list->AddRect(min, max, colour, 0.f, 0, 2.f); break;
            }

            if (payload->IsDelivery()) {
                pending_move_ = std::move(move);
            }
        }
    }

    ImGui::EndDragDropTarget();
}

auto entity_tree_panel::plan_move_(
    const std::string& dragged, const std::string& target, drop_place place
) const -> std::optional<move_entity_params> {
    const auto& scene = state_->scene;

    const auto dragged_it = scene.name_to_entity.find(dragged);
    const auto target_it  = scene.name_to_entity.find(target);
    if (dragged_it == scene.name_to_entity.end() || target_it == scene.name_to_entity.end() ||
        dragged == target || dragged == scene.root_name) {
        return std::nullopt;
    }

    const auto& world = engine_->get_world();

    const auto parent_of = [&world](ecs::entity ent) -> ecs::entity {
        return world.has<ecs::hierarchy_component>(ent)
                   ? world.get<ecs::hierarchy_component>(ent).get_parent()
                   : ecs::invalid_entity;
    };

    // Узел нельзя уложить в собственное поддерево: такой цикл движок отвергает
    // исключением, а подсказка не должна его даже предлагать.
    for (auto ent = target_it->second; ent.is_valid(); ent = parent_of(ent)) {
        if (ent == dragged_it->second) {
            return std::nullopt;
        }
    }

    const auto new_parent =
        place == drop_place::inside ? target_it->second : parent_of(target_it->second);
    if (!new_parent.is_valid() || !world.has<ecs::hierarchy_component>(new_parent)) {
        return std::nullopt;
    }

    const auto parent_name = scene.entity_to_name.find(new_parent);
    if (parent_name == scene.entity_to_name.end()) {
        return std::nullopt;
    }

    // Место считается по списку без переносимого узла — так же, как его читает
    // операция и сам движок.
    const auto& children = world.get<ecs::hierarchy_component>(new_parent).get_children();

    std::vector<ecs::entity> siblings = children;
    std::erase(siblings, dragged_it->second);

    std::size_t index = siblings.size();
    if (place != drop_place::inside) {
        const auto at = std::ranges::find(siblings, target_it->second);
        index = static_cast<std::size_t>(std::distance(siblings.begin(), at)) +
            (place == drop_place::after ? 1 : 0);
    }

    // Бросок туда, где узел и так стоит, в историю не ложится.
    if (parent_of(dragged_it->second) == new_parent) {
        const auto current = static_cast<std::size_t>(
            std::distance(children.begin(), std::ranges::find(children, dragged_it->second))
        );
        if (current == index) {
            return std::nullopt;
        }
    }

    return move_entity_params{
        .name        = dragged,
        .parent_name = parent_name->second,
        .index       = index,
    };
}

}  // namespace vw::sculptor
