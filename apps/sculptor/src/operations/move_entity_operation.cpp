module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

move_entity_operation::move_entity_operation(
    engine_type& engine, app_state& state, const move_entity_params& params
)
    : engine_(&engine), state_(&state), params_(params) {}

auto move_entity_operation::execute() -> void {
    auto& world = engine_->get_world();

    const auto node   = state_->scene.name_to_entity.find(params_.name);
    const auto parent = state_->scene.name_to_entity.find(params_.parent_name);
    if (node == state_->scene.name_to_entity.end() ||
        parent == state_->scene.name_to_entity.end()) {
        return;
    }

    const auto ent = node->second;

    previous_parent_ = world.get<ecs::hierarchy_component>(ent).get_parent();
    if (!previous_parent_.is_valid()) {
        return;
    }

    const auto& siblings = world.get<ecs::hierarchy_component>(previous_parent_).get_children();
    previous_index_ =
        static_cast<std::size_t>(std::distance(siblings.begin(), std::ranges::find(siblings, ent)));

    const auto& node_transform = world.get<ecs::transform_component>(ent);
    previous_local_            = node_transform.get_transform();

    // Вырожденный родитель — нулевой масштаб — обратной матрицы не имеет, и узел
    // тогда переезжает со своими числами как есть.
    auto local = previous_local_;
    const auto inverse =
        math::inverse_matrix(world.get<ecs::transform_component>(parent->second).get_world_matrix());
    if (inverse.has_value()) {
        local = transform::from_matrix(*inverse * node_transform.get_world_matrix());
    }

    place_(parent->second, params_.index, local);
}

auto move_entity_operation::undo() -> void {
    if (!previous_parent_.is_valid()) {
        return;
    }
    place_(previous_parent_, previous_index_, previous_local_);
}

auto move_entity_operation::place_(
    ecs::entity parent, std::size_t index, const transform& local
) -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity.at(params_.name);

    world.system<ecs::hierarchy_system>().modify(ent).set_parent(parent, index);
    world.system<ecs::transform_system>().modify(ent).set_transform(local);

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
