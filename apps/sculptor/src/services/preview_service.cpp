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

[[nodiscard]] auto refuse(
    std::string message
) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

[[nodiscard]] auto joined(
    const std::vector<std::string>& names
) -> std::string {
    std::string list;
    for (const std::string& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list.empty() ? std::string{"none"} : list;
}

}  // namespace

preview_service::preview_service(
    engine_type& eng, app_state& state, asset::model_library& library
)
    : engine_(&eng), state_(&state), library_(&library) {}

auto preview_service::socket_of_(
    std::string_view node, std::string_view socket
) const -> std::expected<ecs::entity, std::string> {
    const auto found = state_->scene.name_to_entity.find(std::string{node});
    if (found == state_->scene.name_to_entity.end()) {
        return refuse(
            std::format(
                "there is no node '{}'; the nodes are: {}", node, joined(list_node_names(*state_))
            )
        );
    }

    auto& world = engine_->get_world();

    std::vector<std::string> sockets;
    if (world.has<ecs::socket_component>(found->second)) {
        for (const ecs::socket_point& point :
             world.get<ecs::socket_component>(found->second).get_sockets()) {
            sockets.push_back(point.name);
        }
    }
    if (!std::ranges::contains(sockets, socket)) {
        return refuse(
            std::format(
                "the node '{}' has no socket '{}'; its sockets are: {}",
                node,
                socket,
                joined(sockets)
            )
        );
    }
    return found->second;
}

auto preview_service::show(
    std::string_view node, std::string_view socket, std::string_view prefab
) -> outcome {
    const auto holder = socket_of_(node, socket);
    if (!holder) {
        return refuse(holder.error());
    }

    const auto filename = prefab_filename(prefab);
    if (!filename) {
        return refuse(std::format("'{}' cannot name a prefab file", prefab));
    }

    namespace fs = std::filesystem;

    const fs::path filepath = app_state::prefab_dir() / fs::path{*filename};

    std::error_code ec;
    if (!fs::is_regular_file(filepath, ec)) {
        return refuse(
            std::format(
                "there is no prefab '{}'; assets_list names the prefabs there are", *filename
            )
        );
    }

    auto& world = engine_->get_world();

    const std::string key =
        socket_state::socket_preview_key(std::string{node}, std::string{socket});
    state_->sockets.erase_preview(key, world);

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};

    const ecs::vox_deserializer::options opts{
        .skip_tags = {"socket", "anim_target"},
    };

    auto loaded = deserializer.deserialize(filepath, opts);
    if (!loaded.has_value()) {
        return refuse(std::format("the prefab '{}' does not load", *filename));
    }

    auto& variants = world.system<ecs::variant_system>();
    for (const ecs::entity ent : loaded->entities) {
        variants.mark_content(ent, *holder);
    }

    socket_state::socket_preview preview;
    preview.filename          = *filename;
    preview.preview_root_name = loaded->root_name;
    preview.entities          = std::move(loaded->entities);

    if (const auto root = loaded->name_to_entity.find(loaded->root_name);
        root != loaded->name_to_entity.end()) {
        const ecs::socket_point* point =
            world.get<ecs::socket_component>(*holder).find(std::string{socket});

        world.system<ecs::transform_system>()
            .modify(root->second)
            .set_position(point->position)
            .set_rotation(point->rotation)
            .set_scale(point->scale);
        world.system<ecs::hierarchy_system>().modify(root->second).set_parent(*holder);
    }

    state_->sockets.socket_previews[key] = std::move(preview);
    return {};
}

auto preview_service::hide(
    std::string_view node, std::string_view socket
) -> outcome {
    const auto holder = socket_of_(node, socket);
    if (!holder) {
        return refuse(holder.error());
    }

    state_->sockets.erase_preview(
        socket_state::socket_preview_key(std::string{node}, std::string{socket}),
        engine_->get_world()
    );
    return {};
}

auto preview_service::shown_in(
    std::string_view node, std::string_view socket
) const -> std::optional<std::string> {
    const auto found = state_->sockets.socket_previews.find(
        socket_state::socket_preview_key(std::string{node}, std::string{socket})
    );
    if (found == state_->sockets.socket_previews.end()) {
        return std::nullopt;
    }
    return found->second.filename;
}

}  // namespace vw::sculptor
