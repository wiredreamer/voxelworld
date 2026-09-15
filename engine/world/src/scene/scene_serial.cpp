module vw.world;


import std;
import vw.core;
import vw.asset;


namespace vw::ecs {

namespace detail {
constexpr log::log_category vox_deserializer_lc{"vox_deserializer"};
}  // namespace detail

vox_serializer::vox_serializer(
    world& world, asset::vox_writer& writer, entity root, options opts,
    const component_registry& codecs
)
    : world_(&world)
    , writer_(&writer)
    , root_(root)
    , excluded_(std::move(opts.excluded))
    , codecs_(&codecs) {
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

    asset::vox_entity_data data;
    data.name = entity_names_.at(ent);

    if (hierarchy_comp.has_parent()) {
        data.parent_name = entity_names_.at(hierarchy_comp.get_parent());
    }

    extract_node(*world_, ent, data, *codecs_);

    return data;
}

}  // namespace vw::ecs


namespace vw::ecs {


vox_deserializer::vox_deserializer(
    world& world, asset::vox_parser& parser, asset::model_library& library,
    const component_registry& codecs
)
    : world_(&world), parser_(&parser), library_(&library), codecs_(&codecs) {}

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

    // Проходов три, и это фазы, а не удобство. Сущности заводятся все сразу: в
    // один проход родитель обязан стоять в файле раньше ребёнка, а нарушение
    // этого порядка теряло связь молча. Дерево связывается до трансформов,
    // трансформы — до всего прочего: позу покоя цель анимации берёт из уже
    // выставленного трансформа.
    for (const auto& ent_data : prefab.entities) {
        create_entity_(ent_data, res);
    }

    for (const auto& ent_data : prefab.entities) {
        link_parent_(ent_data, res);
    }

    for (const auto phase : {apply_phase::transform, apply_phase::general}) {
        for (const auto& ent_data : prefab.entities) {
            apply_node_phase(
                *world_, res.name_to_entity[ent_data.name], ent_data, *library_, *codecs_, phase,
                opts.skip_tags
            );
        }
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

auto vox_deserializer::link_parent_(
    const asset::vox_entity_data& data, result& res
) -> void {
    if (data.parent_name.empty()) {
        return;
    }

    const auto parent_it = res.name_to_entity.find(data.parent_name);
    if (parent_it == res.name_to_entity.end()) {
        log::warn(
            detail::vox_deserializer_lc, "entity '{}' refers to a missing parent '{}'", data.name,
            data.parent_name
        );
        return;
    }

    world_->system<hierarchy_system>().modify(res.name_to_entity[data.name]).set_parent(parent_it->second);
}

}  // namespace vw::ecs

