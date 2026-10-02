module vw.sculptor;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

set_structure_operation::set_structure_operation(
    engine_type& engine, app_state& st, set_structure_params params
)
    : engine_(&engine), state_(&st), params_(std::move(params)) {}

auto set_structure_operation::execute() -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params_.name];

    previous_.name = params_.name;

    existed_before_ = world.has<ecs::structure_component>(ent);
    if (existed_before_) {
        const auto& structure = world.get<ecs::structure_component>(ent);
        const auto races      = structure.get_races();

        previous_.type  = structure.get_type();
        previous_.races = {races.begin(), races.end()};
        previous_.tier  = structure.get_tier();
        previous_.size  = structure.get_size();
    }

    apply_(params_);
}

auto set_structure_operation::undo() -> void {
    if (existed_before_) {
        apply_(previous_);
        return;
    }

    engine_->get_world()
        .modify(state_->scene.name_to_entity[params_.name])
        .without<ecs::structure_component>();
    state_->file.has_unsaved_changes = true;
}

auto set_structure_operation::apply_(
    const set_structure_params& params
) -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params.name];

    if (!world.has<ecs::structure_component>(ent)) {
        world.modify(ent).with<ecs::structure_component>();
    }

    auto structure = world.system<ecs::structure_system>().modify(ent);
    structure.set_type(params.type);
    structure.set_races(params.races);
    structure.set_tier(params.tier);
    structure.set_size(params.size);

    state_->file.has_unsaved_changes = true;
}

remove_structure_operation::remove_structure_operation(
    engine_type& engine, app_state& st, remove_structure_params params
)
    : engine_(&engine), state_(&st), params_(std::move(params)) {}

auto remove_structure_operation::execute() -> void {
    removed_.reset();

    const auto node = state_->scene.name_to_entity.find(params_.name);
    if (node == state_->scene.name_to_entity.end()) {
        return;
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::structure_component>(node->second)) {
        return;
    }

    const auto& structure = world.get<ecs::structure_component>(node->second);
    const auto races      = structure.get_races();
    removed_              = set_structure_params{
        .name  = params_.name,
        .type  = structure.get_type(),
        .races = {races.begin(), races.end()},
        .tier  = structure.get_tier(),
        .size  = structure.get_size(),
    };

    world.modify(node->second).without<ecs::structure_component>();
    state_->file.has_unsaved_changes = true;
}

auto remove_structure_operation::undo() -> void {
    const auto node = state_->scene.name_to_entity.find(params_.name);
    if (!removed_ || node == state_->scene.name_to_entity.end()) {
        return;
    }

    auto& world = engine_->get_world();
    world.modify(node->second).with<ecs::structure_component>();

    auto structure = world.system<ecs::structure_system>().modify(node->second);
    structure.set_type(removed_->type);
    structure.set_races(removed_->races);
    structure.set_tier(removed_->tier);
    structure.set_size(removed_->size);

    state_->file.has_unsaved_changes = true;
}

set_point_operation::set_point_operation(
    engine_type& engine, app_state& st, set_point_params params
)
    : engine_(&engine), state_(&st), params_(std::move(params)) {}

auto set_point_operation::execute() -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params_.name];

    previous_.name = params_.name;
    previous_.kind = params_.kind;

    if (params_.kind == point_kind::furniture) {
        previous_.present = world.has<ecs::furniture_point_component>(ent);
        if (previous_.present) {
            previous_.tag = world.get<ecs::furniture_point_component>(ent).get_category();
        }
    } else {
        previous_.present = world.has<ecs::connection_point_component>(ent);
        if (previous_.present) {
            previous_.tag = world.get<ecs::connection_point_component>(ent).get_profile();
        }
    }

    apply_(params_);
}

auto set_point_operation::undo() -> void {
    apply_(previous_);
}

auto set_point_operation::apply_(
    const set_point_params& params
) -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params.name];
    auto& system   = world.system<ecs::structure_system>();

    if (params.kind == point_kind::furniture) {
        if (!params.present) {
            world.modify(ent).without<ecs::furniture_point_component>();
        } else {
            if (!world.has<ecs::furniture_point_component>(ent)) {
                world.modify(ent).with<ecs::furniture_point_component>();
            }
            system.set_furniture_category(ent, params.tag);
        }
    } else {
        if (!params.present) {
            world.modify(ent).without<ecs::connection_point_component>();
        } else {
            if (!world.has<ecs::connection_point_component>(ent)) {
                world.modify(ent).with<ecs::connection_point_component>();
            }
            system.set_connection_profile(ent, params.tag);
        }
    }

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
