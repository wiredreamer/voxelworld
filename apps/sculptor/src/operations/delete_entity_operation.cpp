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

[[nodiscard]] auto collect_subtree(ecs::world& world, ecs::entity top) -> std::vector<ecs::entity> {
    std::vector<ecs::entity> found{top};

    for (std::size_t at = 0; at < found.size(); ++at) {
        const ecs::entity ent = found[at];
        if (!world.has<ecs::hierarchy_component>(ent)) {
            continue;
        }

        const std::vector<ecs::entity> children =
            world.get<ecs::hierarchy_component>(ent).get_children();
        found.insert(found.end(), children.begin(), children.end());
    }

    return found;
}

}  // namespace

delete_entity_operation::delete_entity_operation(
    engine_type& engine, app_state& state, asset::model_library& library,
    const delete_entity_params& params
)
    : engine_(&engine), state_(&state), library_(&library), params_(params) {}

auto delete_entity_operation::execute() -> void {
    auto& scene = state_->scene;

    const auto node = scene.name_to_entity.find(params_.name);
    if (node == scene.name_to_entity.end()) {
        return;
    }

    auto& world           = engine_->get_world();
    const ecs::entity top = node->second;

    parent_name_.clear();
    index_among_siblings_ = 0;

    const ecs::entity parent = world.get<ecs::hierarchy_component>(top).get_parent();
    if (parent.is_valid()) {
        if (const auto named = scene.entity_to_name.find(parent); named != scene.entity_to_name.end()) {
            parent_name_ = named->second;
        }

        const auto& siblings  = world.get<ecs::hierarchy_component>(parent).get_children();
        index_among_siblings_ = static_cast<std::size_t>(
            std::distance(siblings.begin(), std::ranges::find(siblings, top))
        );
    }

    was_root_ = scene.root_name == params_.name;

    asset::vox_writer_plain writer;
    snapshot_ = ecs::vox_serializer{world, writer, top, {.entity_names = scene.entity_to_name}}
                    .extract();

    saved_nodes_.clear();
    for (const asset::vox_entity_data& data : snapshot_.entities) {
        const ecs::entity ent = scene.name_to_entity.at(data.name);

        saved_node saved{
            .name      = data.name,
            .placement = world.get<ecs::transform_component>(ent).get_transform(),
            .volume    = {},
            .source    = {},
        };

        if (world.has<ecs::model_component>(ent)) {
            const auto& model_comp = world.get<ecs::model_component>(ent);
            if (model_comp.has_model()) {
                saved.volume = model_comp.get_model();
                saved.source = model_comp.get_source();
            }
        }

        saved_nodes_.push_back(std::move(saved));
    }

    for (const saved_node& saved : saved_nodes_) {
        const ecs::entity ent = scene.name_to_entity.at(saved.name);

        state_->sockets.erase_previews_for(saved.name, world);
        scene.hidden_nodes.erase(saved.name);
        scene.entity_to_name.erase(ent);
        scene.name_to_entity.erase(saved.name);
        std::erase(scene.entities, ent);
    }

    for (const ecs::entity ent : collect_subtree(world, top) | std::views::reverse) {
        if (world.registry().alive(ent)) {
            world.destroy(ent);
        }
    }

    if (was_root_) {
        scene.root_name.clear();
    }
    scene.selected_name = parent_name_;

    state_->file.has_unsaved_changes = true;
}

auto delete_entity_operation::undo() -> void {
    if (saved_nodes_.empty()) {
        return;
    }

    auto& scene = state_->scene;
    auto& world = engine_->get_world();

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};
    auto restored = deserializer.instantiate(snapshot_, {});

    auto& transform_sys = world.system<ecs::transform_system>();
    auto& model_sys     = world.system<ecs::model_system>();

    for (const saved_node& saved : saved_nodes_) {
        const auto found = restored.name_to_entity.find(saved.name);
        if (found == restored.name_to_entity.end()) {
            continue;
        }
        const ecs::entity ent = found->second;

        transform_sys.modify(ent).set_transform(saved.placement);
        if (world.has<ecs::animation_target_component>(ent)) {
            world.system<ecs::animation_system>().modify_target(ent).set_rest_transform(
                saved.placement
            );
        }

        if (saved.volume == nullptr) {
            continue;
        }
        if (!world.has<ecs::model_component>(ent)) {
            world.modify(ent).with<ecs::model_component>();
        }
        if (world.get<ecs::model_component>(ent).get_model() != saved.volume) {
            if (saved.source.empty()) {
                model_sys.modify(ent).set_model(saved.volume);
            } else {
                model_sys.modify(ent).set_model(saved.volume, saved.source);
            }
        }
    }

    const auto top    = restored.name_to_entity.find(params_.name);
    const auto parent = scene.name_to_entity.find(parent_name_);
    if (top != restored.name_to_entity.end() && parent != scene.name_to_entity.end()) {
        world.system<ecs::hierarchy_system>().modify(top->second).set_parent(
            parent->second, index_among_siblings_
        );
    }

    for (const auto& [name, ent] : restored.name_to_entity) {
        scene.name_to_entity[name] = ent;
        scene.entity_to_name[ent]  = name;
        scene.entities.push_back(ent);
    }

    if (was_root_) {
        scene.root_name = params_.name;
    }
    scene.selected_name = params_.name;

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
