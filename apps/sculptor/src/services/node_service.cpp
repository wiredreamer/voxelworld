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

constexpr uint8 max_structure_tier = 5;

[[nodiscard]] auto is_plain_name(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char symbol) {
        const bool letter = (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z');
        const bool digit  = symbol >= '0' && symbol <= '9';
        return letter || digit || symbol == '_' || symbol == '-' || symbol == '.';
    });
}

[[nodiscard]] auto joined(const std::vector<std::string>& names) -> std::string {
    std::string list;
    for (const std::string& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list;
}

[[nodiscard]] auto refuse(std::string message) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

[[nodiscard]] auto is_identity(const transform& placement) -> bool {
    return placement.get_position() == vec3f{} && placement.get_rotation() == quat{} &&
        placement.get_scale() == vec3f{1.0F, 1.0F, 1.0F};
}

}  // namespace

node_service::node_service(
    engine_type& eng, app_state& state, asset::model_library& library,
    operation_manager& op_manager, clip_service& clips
)
    : engine_(&eng),
      state_(&state),
      library_(&library),
      op_manager_(&op_manager),
      clips_(&clips) {}

auto list_node_names(const app_state& state) -> std::vector<std::string> {
    std::vector<std::string> listed;
    listed.reserve(state.scene.name_to_entity.size());
    for (const auto& name : state.scene.name_to_entity | std::views::keys) {
        listed.push_back(name);
    }
    std::ranges::sort(listed);
    return listed;
}

auto node_rename_problem(const app_state& state, std::string_view name, std::string_view new_name)
    -> std::optional<std::string> {
    if (!state.scene.name_to_entity.contains(std::string{name})) {
        return std::format("there is no node '{}'", name);
    }
    if (!is_plain_name(new_name)) {
        return std::format(
            "'{}' cannot name a node: use letters, digits, '_', '-' and '.'", new_name
        );
    }
    if (state.scene.name_to_entity.contains(std::string{new_name})) {
        return std::format("a node '{}' already exists", new_name);
    }
    return std::nullopt;
}

auto leave_edit_contexts(app_state& state, clip_service& clips) -> void {
    clips.stop_machines();

    const bool inside_clip = std::ranges::any_of(state.ctx.stack, [](const edit_context& ctx) {
        return ctx.kind == edit_kind::clip;
    });

    if (inside_clip) {
        clips.exit_animation_mode();
        state.ui.show_timeline = false;
        return;
    }
    state.ctx.leave_to(0);
}

auto node_service::names() const -> std::vector<std::string> {
    return list_node_names(*state_);
}

auto node_service::require_document_() -> outcome {
    if (state_->file.filename.empty()) {
        return refuse("no prefab is open; use prefab_open or prefab_new");
    }
    if (state_->paste.active() || state_->ctx.in_paste()) {
        return refuse("a paste is being placed; confirm or cancel it in the editor");
    }

    leave_edit_contexts(*state_, *clips_);
    return {};
}

auto node_service::find_(std::string_view name) const -> std::expected<ecs::entity, std::string> {
    const auto found = state_->scene.name_to_entity.find(std::string{name});
    if (found == state_->scene.name_to_entity.end()) {
        return refuse(std::format("there is no node '{}'; the nodes are: {}", name, joined(names())));
    }
    return found->second;
}

auto node_service::run_(parts steps) -> void {
    if (steps.empty()) {
        return;
    }
    if (steps.size() == 1) {
        op_manager_->execute(std::move(steps.front()));
        return;
    }
    op_manager_->execute(std::make_unique<composite_operation>(std::move(steps)));
}

auto node_service::create(const node_spec& spec) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }

    const auto& scene = state_->scene;

    if (!is_plain_name(spec.name)) {
        return refuse(std::format(
            "'{}' cannot name a node: use letters, digits, '_', '-' and '.'", spec.name
        ));
    }
    if (scene.name_to_entity.contains(spec.name)) {
        return refuse(std::format("a node '{}' already exists", spec.name));
    }

    const bool becomes_root = scene.root_name.empty();
    if (becomes_root && !spec.parent.empty()) {
        return refuse(
            "the prefab has no root yet: create the first node without a parent, it becomes the root"
        );
    }
    if (!becomes_root) {
        if (spec.parent.empty()) {
            return refuse(std::format("name the parent; the root node is '{}'", scene.root_name));
        }
        if (const auto parent = find_(spec.parent); !parent) {
            return refuse(parent.error());
        }
    }

    parts steps;
    steps.push_back(std::make_unique<create_entity_operation>(
        *engine_, *state_, create_entity_params{.name = spec.name, .parent_name = spec.parent}
    ));

    if (!is_identity(spec.placement)) {
        steps.push_back(std::make_unique<set_transform_operation>(
            *engine_, *state_,
            set_transform_params{.name = spec.name, .new_transform = spec.placement}
        ));
    }

    if (auto planned = plan_components_(spec.name, std::nullopt, spec.components, steps); !planned) {
        return planned;
    }

    run_(std::move(steps));
    return verify_variant_(spec.name, spec.components);
}

auto node_service::remove(std::string_view name) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }
    if (const auto node = find_(name); !node) {
        return refuse(node.error());
    }

    op_manager_->execute(std::make_unique<delete_entity_operation>(
        *engine_, *state_, *library_, delete_entity_params{.name = std::string{name}}
    ));
    return {};
}

auto node_service::rename(std::string_view name, std::string_view new_name) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }
    if (const auto node = find_(name); !node) {
        return refuse(node.error());
    }
    if (name == new_name) {
        return {};
    }
    if (auto problem = node_rename_problem(*state_, name, new_name)) {
        return refuse(std::move(*problem));
    }

    op_manager_->execute(std::make_unique<rename_entity_operation>(
        *engine_, *state_,
        rename_entity_params{.name = std::string{name}, .new_name = std::string{new_name}}
    ));
    return {};
}

auto node_service::duplicate(const duplicate_spec& spec) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }

    const auto& scene = state_->scene;
    auto& world       = engine_->get_world();

    const auto source = find_(spec.name);
    if (!source) {
        return refuse(source.error());
    }
    if (spec.name == scene.root_name) {
        return refuse(std::format(
            "'{}' is the root node; a prefab has one root, so it cannot be copied", spec.name
        ));
    }

    std::vector<ecs::entity> subtree{*source};
    for (std::size_t at = 0; at < subtree.size(); ++at) {
        for (const ecs::entity child : world.get<ecs::hierarchy_component>(subtree[at]).get_children()) {
            if (scene.entity_to_name.contains(child)) {
                subtree.push_back(child);
            }
        }
    }

    std::vector<std::string> unnamed;
    std::vector<std::string> subtree_names;
    for (const ecs::entity ent : subtree) {
        const std::string& name = scene.entity_to_name.at(ent);
        subtree_names.push_back(name);
        if (!spec.names.contains(name)) {
            unnamed.push_back(name);
        }
    }
    if (!unnamed.empty()) {
        std::ranges::sort(unnamed);
        return refuse(std::format(
            "names: every node that is copied needs a new name; missing for: {}", joined(unnamed)
        ));
    }

    std::unordered_set<std::string> taken;
    for (const auto& [from, to] : spec.names) {
        if (!std::ranges::contains(subtree_names, from)) {
            return refuse(std::format(
                "names: '{}' is not '{}' or a node under it", from, spec.name
            ));
        }
        if (!is_plain_name(to)) {
            return refuse(std::format(
                "names: '{}' cannot name a node: use letters, digits, '_', '-' and '.'", to
            ));
        }
        if (scene.name_to_entity.contains(to)) {
            return refuse(std::format("names: a node '{}' already exists", to));
        }
        if (!taken.insert(to).second) {
            return refuse(std::format("names: '{}' is given to two nodes", to));
        }
    }

    for (const auto& [other_name, other] : scene.name_to_entity) {
        if (!world.has<ecs::animation_target_component>(other)) {
            continue;
        }
        const std::string& target = world.get<ecs::animation_target_component>(other).get_name();
        if (taken.contains(target)) {
            return refuse(std::format(
                "names: '{}' is already the animation target of node '{}', and a copied target "
                "takes the name of its node",
                target, other_name
            ));
        }
    }

    if (spec.mirror) {
        for (const ecs::entity ent : subtree) {
            if (world.has<ecs::variant_slot_component>(ent)) {
                return refuse(std::format(
                    "mirror: '{}' holds a variant slot, and its candidates are files that cannot "
                    "be mirrored here; copy without mirror",
                    scene.entity_to_name.at(ent)
                ));
            }
        }
    }

    const ecs::entity source_parent = world.get<ecs::hierarchy_component>(*source).get_parent();
    const std::string parent = spec.parent.value_or(scene.entity_to_name.at(source_parent));
    if (const auto found = find_(parent); !found) {
        return refuse(found.error());
    }

    op_manager_->execute(std::make_unique<duplicate_entity_operation>(
        *engine_, *state_, *library_,
        duplicate_entity_params{
            .name        = spec.name,
            .parent_name = parent,
            .names       = spec.names,
            .mirror      = spec.mirror,
        }
    ));
    return {};
}

auto node_service::reparent(
    std::string_view name, std::string_view parent, std::optional<std::size_t> index
) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }

    const auto node   = find_(name);
    const auto target = find_(parent);
    if (!node) {
        return refuse(node.error());
    }
    if (!target) {
        return refuse(target.error());
    }
    if (name == state_->scene.root_name) {
        return refuse(std::format("'{}' is the root node and cannot be moved", name));
    }

    auto& world = engine_->get_world();

    for (ecs::entity ent = *target; ent.is_valid();
         ent             = world.get<ecs::hierarchy_component>(ent).get_parent()) {
        if (ent == *node) {
            return refuse(std::format(
                "'{}' cannot go under '{}': that is the node itself or one of its descendants", name,
                parent
            ));
        }
    }

    const auto& children = world.get<ecs::hierarchy_component>(*target).get_children();

    std::vector<ecs::entity> others = children;
    std::erase(others, *node);
    const std::size_t place = std::min(index.value_or(others.size()), others.size());

    const bool same_parent = world.get<ecs::hierarchy_component>(*node).get_parent() == *target;
    if (same_parent) {
        const auto current = static_cast<std::size_t>(
            std::distance(children.begin(), std::ranges::find(children, *node))
        );
        if (current == place) {
            return {};
        }
    }

    op_manager_->execute(std::make_unique<move_entity_operation>(
        *engine_, *state_,
        move_entity_params{
            .name                       = std::string{name},
            .parent_name                = std::string{parent},
            .index_among_other_children = place,
        }
    ));
    return {};
}

auto node_service::rest_placement(std::string_view name)
    -> std::expected<transform, std::string> {
    if (auto ready = require_document_(); !ready) {
        return refuse(ready.error());
    }

    const auto node = find_(name);
    if (!node) {
        return refuse(node.error());
    }
    return engine_->get_world().get<ecs::transform_component>(*node).get_transform();
}

auto node_service::set_transform(std::string_view name, const transform& placement) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }
    if (const auto node = find_(name); !node) {
        return refuse(node.error());
    }

    op_manager_->execute(std::make_unique<set_transform_operation>(
        *engine_, *state_,
        set_transform_params{.name = std::string{name}, .new_transform = placement}
    ));
    return {};
}

auto node_service::set_components(std::string_view name, const component_changes& changes)
    -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }

    const auto node = find_(name);
    if (!node) {
        return refuse(node.error());
    }

    const std::string node_name{name};

    parts steps;
    if (auto planned = plan_components_(node_name, *node, changes, steps); !planned) {
        return planned;
    }

    run_(std::move(steps));
    return verify_variant_(node_name, changes);
}

auto node_service::set_rig(std::string_view rig) -> outcome {
    if (auto ready = require_document_(); !ready) {
        return ready;
    }
    if (state_->scene.root_name.empty()) {
        return refuse("the prefab has no nodes yet; the rig is kept on the root node");
    }
    if (!is_plain_name(rig)) {
        return refuse(std::format(
            "'{}' cannot name a rig: use letters, digits, '_', '-' and '.'", rig
        ));
    }

    op_manager_->execute(std::make_unique<set_rig_operation>(
        *engine_, *state_, set_rig_params{.rig_name = std::string{rig}}
    ));
    return {};
}

auto node_service::verify_variant_(const std::string& name, const component_changes& changes)
    -> outcome {
    if (!changes.variant.requested || !changes.variant.value ||
        changes.variant.value->candidates.empty()) {
        return {};
    }

    auto& world     = engine_->get_world();
    const auto node = state_->scene.name_to_entity.find(name);

    const bool applied = node != state_->scene.name_to_entity.end() &&
        world.has<ecs::variant_slot_component>(node->second) &&
        world.get<ecs::variant_slot_component>(node->second).get_selected() ==
            changes.variant.value->selected;
    if (applied) {
        return {};
    }

    op_manager_->undo();
    return refuse(std::format(
        "candidate {} of the variant on '{}' could not be put in place, nothing was changed; a "
        "prefab candidate must offer the targets and sockets the slot requires",
        changes.variant.value->selected, name
    ));
}

auto node_service::plan_components_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    for (const auto plan :
         {&node_service::plan_volume_, &node_service::plan_anim_target_, &node_service::plan_sockets_,
          &node_service::plan_variant_, &node_service::plan_structure_, &node_service::plan_points_}) {
        if (auto planned = (this->*plan)(name, existing, changes, steps); !planned) {
            return planned;
        }
    }
    return {};
}

auto node_service::plan_volume_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    if (!changes.volume.requested) {
        return {};
    }

    auto& world    = engine_->get_world();
    const bool has = existing && world.has<ecs::model_component>(*existing);

    const auto drop_current = [&] {
        if (has) {
            steps.push_back(std::make_unique<remove_model_component_operation>(
                *engine_, *state_, remove_model_component_params{.name = name}
            ));
        }
    };

    if (!changes.volume.value) {
        drop_current();
        return {};
    }

    const volume_spec& wanted = *changes.volume.value;
    if (wanted.blank_size.has_value() == !wanted.source.empty()) {
        return refuse("volume: give either 'size' for a new empty volume or 'ref' for a volume file");
    }

    if (wanted.blank_size) {
        const vec3i size = *wanted.blank_size;
        const auto fits  = [](int32 side) { return side >= 1 && side <= max_volume_side; };
        if (!fits(size.x) || !fits(size.y) || !fits(size.z)) {
            return refuse(std::format(
                "volume.size: each side must be 1..{}, got [{}, {}, {}]", max_volume_side, size.x,
                size.y, size.z
            ));
        }

        drop_current();
        steps.push_back(std::make_unique<add_model_component_operation>(
            *engine_, *state_,
            add_model_component_params{.name = name, .size = size, .fill = voxel{}}
        ));
        return {};
    }

    if (wanted.source.extension() != ".voxm") {
        return refuse(std::format("volume.ref: '{}' is not a .voxm file", wanted.source.str()));
    }
    if (has && world.get<ecs::model_component>(*existing).get_source() == wanted.source) {
        return {};
    }
    if (!library_->load(wanted.source).has_value()) {
        return refuse(std::format(
            "volume.ref: '{}' does not load; assets_list names the volumes there are",
            wanted.source.str()
        ));
    }

    drop_current();
    steps.push_back(std::make_unique<attach_model_operation>(
        *engine_, *state_, *library_, attach_model_params{.name = name, .source = wanted.source}
    ));
    return {};
}

auto node_service::plan_anim_target_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    if (!changes.anim_target.requested) {
        return {};
    }

    auto& world    = engine_->get_world();
    const bool has = existing && world.has<ecs::animation_target_component>(*existing);

    const auto drop_current = [&] {
        if (has) {
            steps.push_back(std::make_unique<remove_animation_target_operation>(
                *engine_, *state_, remove_animation_target_params{.entity_name = name}
            ));
        }
    };

    if (!changes.anim_target.value) {
        drop_current();
        return {};
    }

    const std::string& target = *changes.anim_target.value;
    if (!is_plain_name(target)) {
        return refuse(std::format(
            "anim_target: '{}' cannot name a target: use letters, digits, '_', '-' and '.'", target
        ));
    }
    if (has && world.get<ecs::animation_target_component>(*existing).get_name() == target) {
        return {};
    }

    for (const auto& [other_name, other] : state_->scene.name_to_entity) {
        if (other_name != name && world.has<ecs::animation_target_component>(other) &&
            world.get<ecs::animation_target_component>(other).get_name() == target) {
            return refuse(std::format(
                "anim_target: the target '{}' is already taken by node '{}'", target, other_name
            ));
        }
    }

    drop_current();
    steps.push_back(std::make_unique<add_animation_target_operation>(
        *engine_, *state_,
        add_animation_target_params{.entity_name = name, .target_name = target}
    ));
    return {};
}

auto node_service::plan_sockets_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    if (!changes.sockets.requested) {
        return {};
    }

    auto& world    = engine_->get_world();
    const bool has = existing && world.has<ecs::socket_component>(*existing);

    const bool wanted_none = !changes.sockets.value || changes.sockets.value->empty();
    if (wanted_none) {
        if (has) {
            steps.push_back(std::make_unique<remove_socket_component_operation>(
                *engine_, *state_, remove_socket_component_params{.name = name}
            ));
        }
        return {};
    }

    const std::vector<socket_spec>& wanted = *changes.sockets.value;
    for (std::size_t index = 0; index < wanted.size(); ++index) {
        if (!is_plain_name(wanted[index].name)) {
            return refuse(std::format(
                "sockets[{}].name: '{}' cannot name a socket: use letters, digits, '_', '-' and '.'",
                index, wanted[index].name
            ));
        }
        const auto repeated = std::ranges::count(wanted, wanted[index].name, &socket_spec::name);
        if (repeated > 1) {
            return refuse(std::format("sockets: the name '{}' is given twice", wanted[index].name));
        }
    }

    std::vector<ecs::socket_point> current;
    if (has) {
        current = world.get<ecs::socket_component>(*existing).get_sockets();
    } else {
        steps.push_back(std::make_unique<add_socket_component_operation>(
            *engine_, *state_, add_socket_component_params{.name = name}
        ));
    }

    for (const ecs::socket_point& point : current) {
        if (!std::ranges::contains(wanted, point.name, &socket_spec::name)) {
            steps.push_back(std::make_unique<remove_socket_operation>(
                *engine_, *state_,
                remove_socket_params{.entity_name = name, .socket_name = point.name}
            ));
        }
    }

    for (const socket_spec& socket : wanted) {
        const auto present = std::ranges::find(current, socket.name, &ecs::socket_point::name);
        if (present == current.end()) {
            steps.push_back(std::make_unique<add_socket_operation>(
                *engine_, *state_,
                add_socket_params{
                    .entity_name = name,
                    .socket_name = socket.name,
                    .position    = socket.position,
                    .rotation    = socket.rotation,
                    .scale       = socket.scale,
                }
            ));
            continue;
        }

        const bool moved = present->position != socket.position ||
            present->rotation != socket.rotation || present->scale != socket.scale;
        if (moved) {
            steps.push_back(std::make_unique<set_socket_transform_operation>(
                *engine_, *state_,
                set_socket_transform_params{
                    .entity_name = name,
                    .socket_name = socket.name,
                    .position    = socket.position,
                    .rotation    = socket.rotation,
                    .scale       = socket.scale,
                }
            ));
        }
    }
    return {};
}

auto node_service::plan_variant_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    if (!changes.variant.requested) {
        return {};
    }

    auto& world    = engine_->get_world();
    const bool has = existing && world.has<ecs::variant_slot_component>(*existing);

    if (!changes.variant.value) {
        if (has) {
            steps.push_back(std::make_unique<remove_variant_slot_operation>(
                *engine_, *state_, *library_, remove_variant_slot_params{.name = name}
            ));
        }
        return {};
    }

    const variant_spec& wanted = *changes.variant.value;

    for (std::size_t index = 0; index < wanted.candidates.size(); ++index) {
        const asset::asset_ref& candidate = wanted.candidates[index];
        const auto extension              = candidate.extension();
        if (extension != ".voxm" && extension != ".vox") {
            return refuse(std::format(
                "variant.candidates[{}]: '{}' is neither a .voxm volume nor a .vox prefab", index,
                candidate.str()
            ));
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(library_->path_of(candidate), ec)) {
            return refuse(std::format(
                "variant.candidates[{}]: there is no file '{}'", index, candidate.str()
            ));
        }
    }

    const bool selects_nothing = wanted.candidates.empty() && wanted.selected == 0;
    if (!selects_nothing && wanted.selected >= wanted.candidates.size()) {
        return refuse(std::format(
            "variant.selected: {} is out of range, there are {} candidates", wanted.selected,
            wanted.candidates.size()
        ));
    }

    bool same_candidates = false;
    bool same_selection  = false;
    if (has) {
        const auto& slot = world.get<ecs::variant_slot_component>(*existing);
        same_candidates  = slot.get_candidates() == wanted.candidates;
        same_selection   = slot.get_selected() == wanted.selected;
    } else {
        steps.push_back(std::make_unique<add_variant_slot_operation>(
            *engine_, *state_, add_variant_slot_params{.name = name}
        ));
    }

    if (!same_candidates) {
        steps.push_back(std::make_unique<set_variant_candidates_operation>(
            *engine_, *state_, *library_,
            set_variant_candidates_params{.name = name, .candidates = wanted.candidates}
        ));
    }
    if (!wanted.candidates.empty() && !(same_candidates && same_selection)) {
        steps.push_back(std::make_unique<select_variant_operation>(
            *engine_, *state_, *library_,
            select_variant_params{.name = name, .index = wanted.selected}
        ));
    }
    return {};
}

auto node_service::plan_structure_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    if (!changes.structure.requested) {
        return {};
    }

    auto& world    = engine_->get_world();
    const bool has = existing && world.has<ecs::structure_component>(*existing);

    if (!changes.structure.value) {
        if (has) {
            steps.push_back(std::make_unique<remove_structure_operation>(
                *engine_, *state_, remove_structure_params{.name = name}
            ));
        }
        return {};
    }

    const structure_spec& wanted = *changes.structure.value;
    if (wanted.tier > max_structure_tier) {
        return refuse(std::format(
            "structure.tier: must be 0..{}, got {}", max_structure_tier, wanted.tier
        ));
    }

    steps.push_back(std::make_unique<set_structure_operation>(
        *engine_, *state_,
        set_structure_params{
            .name  = name,
            .type  = wanted.type,
            .races = wanted.races,
            .tier  = wanted.tier,
            .size  = wanted.size,
        }
    ));
    return {};
}

auto node_service::plan_points_(
    const std::string& name, std::optional<ecs::entity> existing, const component_changes& changes,
    parts& steps
) const -> outcome {
    auto& world = engine_->get_world();

    const auto plan = [&](
                          const component_change<std::string>& change, point_kind kind,
                          std::string_view field, bool has
                      ) -> outcome {
        if (!change.requested) {
            return {};
        }
        if (!change.value) {
            if (has) {
                steps.push_back(std::make_unique<set_point_operation>(
                    *engine_, *state_,
                    set_point_params{.name = name, .kind = kind, .tag = {}, .present = false}
                ));
            }
            return {};
        }
        if (change.value->empty()) {
            return refuse(std::format("{}: give a name, or null to remove the point", field));
        }

        steps.push_back(std::make_unique<set_point_operation>(
            *engine_, *state_,
            set_point_params{.name = name, .kind = kind, .tag = *change.value, .present = true}
        ));
        return {};
    };

    const bool has_furniture = existing && world.has<ecs::furniture_point_component>(*existing);
    const bool has_connection = existing && world.has<ecs::connection_point_component>(*existing);

    if (auto planned = plan(changes.furniture, point_kind::furniture, "furniture", has_furniture);
        !planned) {
        return planned;
    }
    return plan(changes.connection, point_kind::connection, "connection", has_connection);
}

}  // namespace vw::sculptor
