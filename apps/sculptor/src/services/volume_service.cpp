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

[[nodiscard]] auto refuse(std::string message) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
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

}  // namespace

volume_service::volume_service(
    engine_type& eng, app_state& state, operation_manager& op_manager, file_service& files,
    clip_service& clips
)
    : engine_(&eng),
      state_(&state),
      op_manager_(&op_manager),
      files_(&files),
      clips_(&clips) {}

auto volume_service::find(std::string_view node) const -> std::expected<volume, std::string> {
    const auto found = state_->scene.name_to_entity.find(std::string{node});
    if (found == state_->scene.name_to_entity.end()) {
        return refuse(std::format(
            "there is no node '{}'; the nodes are: {}", node, joined(list_node_names(*state_))
        ));
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::model_component>(found->second) ||
        !world.get<ecs::model_component>(found->second).has_model()) {
        return refuse(std::format(
            "the node '{}' has no volume; give it one with node_set_components", node
        ));
    }
    return world.get<ecs::model_component>(found->second).get_model();
}

auto volume_service::holders(const volume& held) const -> std::vector<std::string> {
    auto& world = engine_->get_world();

    std::vector<std::string> names;
    for (const auto& [name, ent] : state_->scene.name_to_entity) {
        if (world.has<ecs::model_component>(ent) &&
            world.get<ecs::model_component>(ent).get_model() == held) {
            names.push_back(name);
        }
    }
    std::ranges::sort(names);
    return names;
}

auto volume_service::enter_(std::string_view node) -> std::expected<volume, std::string> {
    if (state_->file.filename.empty()) {
        return refuse("no prefab is open; use prefab_open or prefab_new");
    }
    if (state_->paste.active() || state_->ctx.in_paste()) {
        return refuse("a paste is being placed; confirm or cancel it in the editor");
    }

    auto held = find(node);
    if (!held) {
        return held;
    }

    const bool already_inside =
        state_->ctx.kind() == edit_kind::model && state_->ctx.node_name() == node;
    if (!already_inside) {
        leave_edit_contexts(*state_, *clips_);
        state_->ctx.enter(edit_context::model(std::string{node}));
    }
    return held;
}

auto volume_service::write(std::string_view node, std::vector<asset::voxel_edit> edits) -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }
    if (edits.empty()) {
        return refuse("there is nothing to write");
    }

    const vec3i size = (*held)->size();
    for (const asset::voxel_edit& edit : edits) {
        if (!asset::contains(size, edit.position)) {
            return refuse(std::format(
                "[{}, {}, {}] is outside the volume of '{}', which spans [0, 0, 0] to [{}, {}, {}]; "
                "grow it with volume_reshape first",
                edit.position.x, edit.position.y, edit.position.z, node, size.x - 1, size.y - 1,
                size.z - 1
            ));
        }
    }

    op_manager_->execute(std::make_unique<edit_voxels_operation>(
        *engine_, *state_, edit_voxels_params{.name = std::string{node}, .edits = std::move(edits)}
    ));
    return {};
}

auto volume_service::resize(std::string_view node, vec3i grown_at_min, vec3i grown_at_max)
    -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }
    if (grown_at_min == vec3i{} && grown_at_max == vec3i{}) {
        return refuse("there is nothing to resize: every amount is zero");
    }

    const vec3i old_size = (*held)->size();
    const vec3i size{
        old_size.x + grown_at_min.x + grown_at_max.x,
        old_size.y + grown_at_min.y + grown_at_max.y,
        old_size.z + grown_at_min.z + grown_at_max.z,
    };

    const auto fits = [](int32 side) { return side >= 1 && side <= max_volume_side; };
    if (!fits(size.x) || !fits(size.y) || !fits(size.z)) {
        return refuse(std::format(
            "the volume would become [{}, {}, {}]; each side must stay within 1..{}", size.x,
            size.y, size.z, max_volume_side
        ));
    }

    op_manager_->execute(std::make_unique<resize_volume_operation>(
        *engine_, *state_,
        resize_volume_params{
            .name         = std::string{node},
            .grown_at_min = grown_at_min,
            .grown_at_max = grown_at_max,
        }
    ));
    return {};
}

auto volume_service::trim(std::string_view node) -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }

    const auto occupied = asset::occupied_bounds(**held);
    if (!occupied) {
        return refuse(std::format("the volume of '{}' is empty, there is nothing to trim to", node));
    }
    if (occupied->size() == (*held)->size()) {
        return {};
    }

    op_manager_->execute(std::make_unique<trim_model_operation>(
        *engine_, *state_, trim_model_params{.name = std::string{node}}
    ));
    return {};
}

auto volume_service::reorient(std::string_view node, const asset::voxel_orientation& how)
    -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }
    if (how == asset::voxel_orientation{}) {
        return {};
    }

    op_manager_->execute(std::make_unique<reorient_model_operation>(
        *engine_, *state_, reorient_model_params{.name = std::string{node}, .how = how}
    ));
    return {};
}

auto volume_service::set_pivot(std::string_view node, const vec3f& pivot) -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }

    op_manager_->execute(std::make_unique<set_pivot_operation>(
        *engine_, *state_, set_pivot_params{.name = std::string{node}, .new_pivot = pivot}
    ));
    return {};
}

auto volume_service::fork(std::string_view node, std::string_view stem, bool overwrite)
    -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }

    std::vector<std::string> sharing = holders(*held);
    std::erase(sharing, node);
    if (sharing.empty()) {
        return refuse(std::format(
            "the volume of '{}' is held by this node alone, there is nothing to split; "
            "volume_rename renames its file",
            node
        ));
    }

    const auto own = files_->free_model_ref(stem, overwrite);
    if (!own) {
        switch (own.error()) {
            case rename_model_error::invalid_name:
                return refuse(std::format(
                    "'{}' cannot name a volume file: use a plain name without separators or dots",
                    stem
                ));
            case rename_model_error::name_in_use:
                return refuse(std::format(
                    "another node or variant of this prefab already uses a volume named '{}'", stem
                ));
            case rename_model_error::file_exists:
                return refuse(std::format(
                    "a volume file named '{}' already exists; pass overwrite: true to replace it",
                    stem
                ));
            case rename_model_error::write_failed:
                return refuse("the prefab has no file yet; save it first");
        }
    }

    op_manager_->execute(std::make_unique<fork_volume_operation>(
        *engine_, *state_, fork_volume_params{.name = std::string{node}, .source = *own}
    ));
    return {};
}

auto volume_service::rename(std::string_view node, std::string_view stem, bool overwrite)
    -> outcome {
    const auto held = enter_(node);
    if (!held) {
        return refuse(held.error());
    }

    const auto ent    = state_->scene.name_to_entity.at(std::string{node});
    const auto source = engine_->get_world().get<ecs::model_component>(ent).get_source();
    if (source.empty()) {
        return refuse(std::format(
            "the volume of '{}' has no file yet; save the prefab first, then rename", node
        ));
    }

    const auto renamed = files_->rename_model(source, stem, overwrite);
    if (renamed) {
        return {};
    }

    switch (renamed.error()) {
        case rename_model_error::invalid_name:
            return refuse(std::format(
                "'{}' cannot name a volume file: use a plain name without separators or dots", stem
            ));
        case rename_model_error::name_in_use:
            return refuse(std::format(
                "another node or variant of this prefab already uses a volume named '{}'", stem
            ));
        case rename_model_error::file_exists:
            return refuse(std::format(
                "a volume file named '{}' already exists; pass overwrite: true to replace it", stem
            ));
        case rename_model_error::write_failed:
            return refuse("the volume or the prefab could not be written; nothing was renamed");
    }
    return refuse("the volume could not be renamed");
}

}  // namespace vw::sculptor
