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
    : world_(&world), writer_(&writer), root_(root), codecs_(&codecs),
      kind_(std::move(opts.kind)) {
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
    prefab.kind      = kind_;

    // Риг — свойство префаба целиком, поэтому он на корне и в шапке, а не в
    // узле: узлов с целями много, риг у них один.
    if (world_->has<rig_component>(root_)) {
        prefab.rig = world_->get<rig_component>(root_).get_name();
    }

    // Автоматы лежат там же, на корне, и по той же причине. Без этого редактор
    // открыл бы префаб с автоматами и сохранил без них.
    if (world_->has<animation_machines_component>(root_)) {
        const auto sources = world_->get<animation_machines_component>(root_).get_sources();
        prefab.fsm_refs.assign(sources.begin(), sources.end());
    }

    std::deque<entity> to_process;
    to_process.push_back(root_);

    while (!to_process.empty()) {
        entity current = to_process.front();
        to_process.pop_front();

        // Содержимое по ссылке в дерево не пишется: его принесёт кандидат слота
        // или превью сокета, а не этот файл. Поддерево такого узла тоже не
        // обходится — оно всё пришло вместе с ним.
        if (world_->has<slot_content_component>(current)) {
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

        if (world_->has<slot_content_component>(current)) {
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
    res.kind      = prefab.kind;

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
    attach_machines_(prefab, res);

    return res;
}

auto vox_deserializer::put_variant(
    entity node, std::size_t index
) -> std::expected<void, variant_error> {
    auto& registry = world_->registry();
    if (!registry.has<variant_slot_component>(node)) {
        return std::unexpected(variant_error::no_slot);
    }

    const auto& slot = registry.get<variant_slot_component>(node);
    if (index >= slot.get_candidates().size()) {
        return std::unexpected(variant_error::out_of_range);
    }

    const auto ref = slot.get_candidates()[index];
    if (ref.extension() != ".vox") {
        // Объём ставит система: разборщик ей для этого не нужен.
        return world_->system<variant_system>().apply(node, *library_, index);
    }

    const auto prefab = parser_->parse(library_->path_of(ref));
    if (!prefab.has_value()) {
        log::warn(detail::vox_deserializer_lc, "failed to read candidate '{}'", ref.str());
        return std::unexpected(variant_error::load_failed);
    }

    const auto report = check_candidate(slot, *prefab);
    if (!report.ok()) {
        log::warn(
            detail::vox_deserializer_lc,
            "candidate '{}' does not close slot '{}': {} target(s) and {} socket(s) missing",
            ref.str(), slot.get_name(), report.missing_targets.size(),
            report.missing_sockets.size()
        );
        return std::unexpected(variant_error::contract_unmet);
    }

    clear_content_(node);

    auto res = instantiate(*prefab, {});

    auto& variants = world_->system<variant_system>();
    for (const auto ent : res.entities) {
        variants.mark_content(ent, node);
    }

    const auto root_it = res.name_to_entity.find(res.root_name);
    if (root_it != res.name_to_entity.end()) {
        world_->system<hierarchy_system>().modify(root_it->second).set_parent(node);
    }

    auto slot_mod = variants.modify(node);
    slot_mod.set_content(std::move(res.entities));
    slot_mod.select(index);

    return {};
}

auto vox_deserializer::clear_content_(
    entity node
) -> void {
    const auto& slot = world_->registry().get<variant_slot_component>(node);

    const auto content = slot.get_content();
    for (const auto ent : content) {
        world_->destroy(ent);
    }

    world_->system<variant_system>().modify(node).set_content({});
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

auto vox_deserializer::attach_machines_(
    const asset::vox_prefab_data& prefab, const result& res
) -> void {
    if (prefab.fsm_refs.empty()) {
        return;
    }

    const auto root_it = res.name_to_entity.find(prefab.root_name);
    if (root_it == res.name_to_entity.end()) {
        return;
    }

    world_->modify(root_it->second).with<animation_machines_component>();
    world_->system<animation_fsm_system>().modify_machines(root_it->second).set(prefab.fsm_refs);
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

