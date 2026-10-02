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

constexpr std::string_view volume_tag = "model";
constexpr std::string_view target_tag = "anim_target";

[[nodiscard]] auto collect_subtree(
    ecs::world& world, ecs::entity top
) -> std::vector<ecs::entity> {
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

[[nodiscard]] auto mirrored(
    const vec3f& position, asset::voxel_axis across
) -> vec3f {
    switch (across) {
        case asset::voxel_axis::x:
            return vec3f{-position.x, position.y, position.z};
        case asset::voxel_axis::y:
            return vec3f{position.x, -position.y, position.z};
        case asset::voxel_axis::z:
            return vec3f{position.x, position.y, -position.z};
    }
    return position;
}

[[nodiscard]] auto mirrored(
    const quat& rotation, asset::voxel_axis across
) -> quat {
    switch (across) {
        case asset::voxel_axis::x:
            return quat{rotation.x, -rotation.y, -rotation.z, rotation.w};
        case asset::voxel_axis::y:
            return quat{-rotation.x, rotation.y, -rotation.z, rotation.w};
        case asset::voxel_axis::z:
            return quat{-rotation.x, -rotation.y, rotation.z, rotation.w};
    }
    return rotation;
}

[[nodiscard]] auto mirrored(
    const transform& placement, asset::voxel_axis across
) -> transform {
    transform flipped = placement;
    flipped.set_position(mirrored(placement.get_position(), across));
    flipped.set_rotation(mirrored(placement.get_rotation(), across));
    return flipped;
}

}  // namespace

duplicate_entity_operation::duplicate_entity_operation(
    engine_type& engine,
    app_state& state,
    asset::model_library& library,
    const duplicate_entity_params& params
)
    : engine_(&engine), state_(&state), library_(&library), params_(params) {}

auto duplicate_entity_operation::execute() -> void {
    auto& scene = state_->scene;
    auto& world = engine_->get_world();

    const auto source = scene.name_to_entity.find(params_.name);
    const auto parent = scene.name_to_entity.find(params_.parent_name);
    if (source == scene.name_to_entity.end() || parent == scene.name_to_entity.end()) {
        return;
    }

    asset::vox_writer_plain writer;
    asset::vox_prefab_data copied = ecs::vox_serializer{
        world, writer, source->second, {.entity_names = scene.entity_to_name}
    }.extract();

    const asset::voxel_orientation turned = params_.mirror ?
        asset::mirrored_orientation(*params_.mirror) :
        asset::rotated_orientation(asset::voxel_axis::y, 0);

    auto& model_reg = world.resource<asset::model_registry>();

    std::vector<copied_node> planned;
    for (asset::vox_entity_data& data : copied.entities) {
        const auto renamed = params_.names.find(data.name);
        if (renamed == params_.names.end()) {
            return;
        }
        const ecs::entity original = scene.name_to_entity.at(data.name);

        copied_node node{
            .name      = renamed->second,
            .placement = world.get<ecs::transform_component>(original).get_transform(),
            .volume    = {},
            .sockets   = {},
        };

        if (world.has<ecs::model_component>(original)) {
            if (const auto held = world.get<ecs::model_component>(original).get_model()) {
                node.volume = asset::reoriented(*held, turned, model_reg);
            }
        }
        if (world.has<ecs::socket_component>(original)) {
            node.sockets = world.get<ecs::socket_component>(original).get_sockets();
        }

        if (params_.mirror) {
            node.placement = mirrored(node.placement, *params_.mirror);
            for (ecs::socket_point& point : node.sockets) {
                point.position = mirrored(point.position, *params_.mirror);
                point.rotation = mirrored(point.rotation, *params_.mirror);
            }
        }

        std::erase_if(data.tags, [](const asset::vox_tag& tag) { return tag.name == volume_tag; });
        for (asset::vox_tag& tag : data.tags) {
            if (tag.name == target_tag) {
                tag.value = renamed->second;
            }
        }

        const auto parent_copy = params_.names.find(data.parent_name);
        data.parent_name = parent_copy == params_.names.end() ? std::string{} : parent_copy->second;
        data.name        = renamed->second;

        planned.push_back(std::move(node));
    }

    copied.root_name = params_.names.at(params_.name);
    copied.rig.clear();
    copied.fsm_refs.clear();

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};
    auto made = deserializer.instantiate(copied, {});

    auto& transform_sys = world.system<ecs::transform_system>();
    auto& model_sys     = world.system<ecs::model_system>();
    auto& socket_sys    = world.system<ecs::socket_system>();

    for (const copied_node& node : planned) {
        const auto found = made.name_to_entity.find(node.name);
        if (found == made.name_to_entity.end()) {
            continue;
        }
        const ecs::entity ent = found->second;

        transform_sys.modify(ent).set_transform(node.placement);
        if (world.has<ecs::animation_target_component>(ent)) {
            world.system<ecs::animation_system>().modify_target(ent).set_rest_transform(
                node.placement
            );
        }

        if (node.volume != nullptr) {
            if (!world.has<ecs::model_component>(ent)) {
                world.modify(ent).with<ecs::model_component>();
            }
            model_sys.modify(ent).set_model(node.volume, asset::asset_ref{});
        }

        if (params_.mirror && world.has<ecs::socket_component>(ent)) {
            for (const ecs::socket_point& point : node.sockets) {
                socket_sys.modify(ent)
                    .remove_socket(point.name)
                    .add_socket(point.name, point.position, point.rotation, point.scale);
            }
        }
    }

    const auto top = made.name_to_entity.find(copied.root_name);
    if (top != made.name_to_entity.end()) {
        world.system<ecs::hierarchy_system>().modify(top->second).set_parent(parent->second);
    }

    made_names_.clear();
    for (const auto& [name, ent] : made.name_to_entity) {
        scene.name_to_entity[name] = ent;
        scene.entity_to_name[ent]  = name;
        scene.entities.push_back(ent);
        made_names_.push_back(name);
    }

    selected_before_    = scene.selected_name;
    scene.selected_name = copied.root_name;

    state_->file.has_unsaved_changes = true;
}

auto duplicate_entity_operation::undo() -> void {
    auto& scene = state_->scene;
    auto& world = engine_->get_world();

    const auto top = scene.name_to_entity.find(params_.names.at(params_.name));
    if (top == scene.name_to_entity.end()) {
        return;
    }
    const std::vector<ecs::entity> doomed = collect_subtree(world, top->second);

    for (const std::string& name : made_names_) {
        const auto found = scene.name_to_entity.find(name);
        if (found == scene.name_to_entity.end()) {
            continue;
        }

        state_->sockets.erase_previews_for(name, world);
        scene.hidden_nodes.erase(name);
        scene.entity_to_name.erase(found->second);
        std::erase(scene.entities, found->second);
        scene.name_to_entity.erase(found);
    }

    for (const ecs::entity ent : doomed | std::views::reverse) {
        if (world.registry().alive(ent)) {
            world.destroy(ent);
        }
    }

    made_names_.clear();
    scene.selected_name = selected_before_;

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
