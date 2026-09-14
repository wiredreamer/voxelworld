module vw.world;


import std;
import vw.core;
import vw.asset;


namespace vw::ecs {

namespace detail {
constexpr log::log_category vox_deserializer_lc{"vox_deserializer"};
}  // namespace detail

vox_serializer::vox_serializer(
    world& world, vox_writer& writer, entity root, options opts
) : world_(&world), writer_(&writer), root_(root), excluded_(std::move(opts.excluded)) {
    if (opts.entity_names.has_value()) {
        entity_names_ = std::move(opts.entity_names.value());
    } else {
        generate_entity_names_();
    }
}

auto vox_serializer::serialize(
    const std::filesystem::path& filepath
) -> std::expected<void, error_type> {
    auto prefab = extract();
    return writer_->write(filepath, prefab);
}

auto vox_serializer::extract() const -> asset::vox_prefab_data {
    asset::vox_prefab_data prefab;
    prefab.root_name = entity_names_.at(root_);

    // Риг — свойство префаба целиком, поэтому он на корне и в шапке, а не в
    // узле: узлов с целями много, риг у них один.
    if (world_->has<rig_component>(root_)) {
        prefab.rig = world_->get<rig_component>(root_).get_name();
    }

    std::deque<entity> to_process;
    to_process.push_back(root_);

    while (!to_process.empty()) {
        entity current = to_process.front();
        to_process.pop_front();

        if (excluded_.contains(current)) {
            continue;
        }

        bool can_be_serialized =
            world_->has<hierarchy_component>(current) &&
            world_->has<transform_component>(current) &&
            world_->has<spatial_component>(current);

        if (can_be_serialized) {
            prefab.entities.push_back(extract_entity_(current));
        }

        if (world_->has<hierarchy_component>(current)) {
            auto& hierarchy = world_->get<hierarchy_component>(current);
            for (const auto& child : hierarchy.get_children()) {
                to_process.push_back(child);
            }
        }
    }

    return prefab;
}

auto vox_serializer::generate_entity_names_() -> void {
    std::deque<entity> to_process;
    to_process.push_back(root_);
    entity_names_[root_] = "root";

    while (!to_process.empty()) {
        entity current = to_process.front();
        to_process.pop_front();

        if (excluded_.contains(current)) {
            continue;
        }

        if (!entity_names_.contains(current)) {
            std::string name = std::format("child_{}", entity_names_.size());
            entity_names_[current] = name;
        }

        if (world_->has<hierarchy_component>(current)) {
            auto& hierarchy = world_->get<hierarchy_component>(current);
            for (const auto& child : hierarchy.get_children()) {
                to_process.push_back(child);
            }
        }
    }
}

auto vox_serializer::extract_entity_(entity ent) const -> asset::vox_entity_data {
    auto& hierarchy_comp = world_->get<hierarchy_component>(ent);
    auto& transform_comp = world_->get<transform_component>(ent);

    asset::vox_entity_data data;
    data.name = entity_names_.at(ent);

    if (hierarchy_comp.has_parent()) {
        entity parent = hierarchy_comp.get_parent();
        data.parent_name = entity_names_.at(parent);
    }

    data.position = transform_comp.get_position();
    data.rotation = transform_comp.get_rotation_euler();
    data.scale = transform_comp.get_scale();
    data.has_transform = true;

    if (world_->has<animation_target_component>(ent)) {
        auto& target = world_->get<animation_target_component>(ent);
        data.animation_target_name = target.get_name();
    }

    if (world_->has<socket_component>(ent)) {
        auto& socket_comp = world_->get<socket_component>(ent);
        data.has_sockets = true;
        for (const auto& sp : socket_comp.get_sockets()) {
            auto rot_euler = math::quat_to_euler(sp.rotation);
            data.sockets.push_back({sp.name, sp.position, rot_euler, sp.scale});
        }
    }

    // Воксели в дереве больше не лежат: узел называет .voxm, а сам объём пишет
    // тот, кто владеет библиотекой. Узел без ссылки законен — её раздаёт первая
    // запись, и до неё он просто ни на что не ссылается.
    if (world_->has<model_component>(ent)) {
        data.model = world_->get<model_component>(ent).get_source();
    }

    return data;
}

}  // namespace vw::ecs


namespace vw::ecs {


vox_deserializer::vox_deserializer(
    world& world, asset::vox_parser& parser, asset::model_library& library
)
    : world_(&world), parser_(&parser), library_(&library) {}

auto vox_deserializer::deserialize(
    const std::filesystem::path& filepath
) -> std::expected<result, error_type> {
    return deserialize(filepath, options{});
}

auto vox_deserializer::deserialize(
    const std::filesystem::path& filepath, const options& opts
) -> std::expected<result, error_type> {
    const auto prefab = parser_->parse(filepath);
    if (!prefab.has_value()) {
        return std::unexpected(prefab.error());
    }

    return instantiate(*prefab, opts);
}

auto vox_deserializer::instantiate(
    const asset::vox_prefab_data& prefab, const options& opts
) -> result {
    result res;
    res.root_name = prefab.root_name;

    // Два прохода: сначала заводятся все сущности, и только потом связывается
    // иерархия. В один проход родитель обязан стоять в файле раньше ребёнка, а
    // нарушение этого порядка теряло связь молча.
    for (const auto& ent_data : prefab.entities) {
        create_entity_(ent_data, res);
    }

    for (const auto& ent_data : prefab.entities) {
        apply_entity_(ent_data, res, opts);
    }

    attach_rig_(prefab, res);

    return res;
}

auto vox_deserializer::attach_rig_(
    const asset::vox_prefab_data& prefab, const result& res
) -> void {
    if (prefab.rig.empty()) {
        return;
    }

    const auto root_it = res.name_to_entity.find(prefab.root_name);
    if (root_it == res.name_to_entity.end()) {
        log::warn(
            detail::vox_deserializer_lc, "prefab names rig '{}' but has no root '{}'", prefab.rig,
            prefab.root_name
        );
        return;
    }

    world_->modify(root_it->second).with<rig_component>();
    world_->system<animation_system>().modify_rig(root_it->second).set_name(prefab.rig);
}

auto vox_deserializer::create_entity_(const asset::vox_entity_data& data, result& res) -> void {
    const auto ent = world_->create()
        .with<hierarchy_component>()
        .with<transform_component>()
        .with<spatial_component>()
        .get_entity();

    res.name_to_entity[data.name] = ent;
    res.entity_to_name[ent]       = data.name;
    res.entities.push_back(ent);
}

auto vox_deserializer::apply_entity_(
    const asset::vox_entity_data& data, result& res, const options& opts
) -> void {
    const auto ent = res.name_to_entity[data.name];

    if (!data.parent_name.empty()) {
        const auto parent_it = res.name_to_entity.find(data.parent_name);
        if (parent_it != res.name_to_entity.end()) {
            auto& hierarchy_sys = world_->system<hierarchy_system>();
            hierarchy_sys.modify(ent).set_parent(parent_it->second);
        } else {
            log::warn(
                detail::vox_deserializer_lc, "entity '{}' refers to a missing parent '{}'",
                data.name, data.parent_name
            );
        }
    }

    if (data.has_transform) {
        auto& transform_sys = world_->system<transform_system>();
        transform_sys.modify(ent)
            .set_position(data.position)
            .set_rotation_euler(data.rotation)
            .set_scale(data.scale);
    }

    if (data.animation_target_name.has_value() && !opts.skip_targets) {
        world_->modify(ent).with<animation_target_component>();
        auto& anim_sys = world_->system<animation_system>();
        auto target_mod = anim_sys.modify_target(ent);
        target_mod.set_target_name(*data.animation_target_name);
        if (data.has_transform) {
            transform rest;
            rest.set_position(data.position);
            rest.set_rotation_euler(data.rotation);
            rest.set_scale(data.scale);
            target_mod.set_rest_transform(rest);
        }
    }

    if (data.has_sockets && !opts.skip_sockets) {
        world_->modify(ent).with<socket_component>();
        auto& socket_sys = world_->system<socket_system>();
        for (const auto& sp : data.sockets) {
            socket_sys.modify(ent).add_socket(
                sp.name, sp.position, math::euler_to_quat(sp.rotation), sp.scale
            );
        }
    }

    attach_model_(data, ent);
}

auto vox_deserializer::attach_model_(const asset::vox_entity_data& data, entity ent) -> void {
    if (data.model.empty()) {
        return;
    }

    // Битая ссылка не повод ронять загрузку: узел встаёт без объёма, о чём
    // сказано в логе, и остальной префаб открывается целиком.
    auto loaded = library_->load(data.model);
    if (!loaded.has_value()) {
        log::warn(
            detail::vox_deserializer_lc, "entity '{}' refers to a missing model '{}'", data.name,
            data.model.str()
        );
        return;
    }

    world_->modify(ent).with<model_component>();
    world_->system<model_system>().modify(ent).set_model(*loaded, data.model);
}

}  // namespace vw::ecs

