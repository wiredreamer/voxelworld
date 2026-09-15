module vw.world;


import std;
import vw.core;
import vw.asset;


namespace vw::ecs {

namespace detail {
constexpr log::log_category vox_deserializer_lc{"vox_deserializer"};

// Значение тега — строка, и сколько в ней чисел, знает только читающий. Здесь
// эти две стороны и живут: формат хранит текст, смысл ему придаёт vw.world.
inline auto write_vec3f(const vec3f& value) -> std::string {
    return std::format("{} {} {}", value.x, value.y, value.z);
}

inline auto read_vec3f(std::string_view text, vec3f fallback) -> vec3f {
    std::array<float32, 3> values{};
    return asset::parse_floats(text, values) ? vec3f{values[0], values[1], values[2]} : fallback;
}

// Трансформ узла — девять чисел одной строкой: позиция, эйлеры, масштаб.
struct node_transform {
    vec3f position;
    vec3f rotation;
    vec3f scale{1.0F, 1.0F, 1.0F};
};

inline auto read_transform(const asset::vox_entity_data& data) -> std::optional<node_transform> {
    const auto* tag = data.find("transform");
    if (tag == nullptr) {
        return std::nullopt;
    }

    std::array<float32, 9> values{};
    if (!asset::parse_floats(tag->value, values)) {
        return std::nullopt;
    }

    return node_transform{
        .position = {values[0], values[1], values[2]},
        .rotation = {values[3], values[4], values[5]},
        .scale    = {values[6], values[7], values[8]},
    };
}
}  // namespace detail

vox_serializer::vox_serializer(
    world& world, asset::vox_writer& writer, entity root, options opts
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

    const auto position = transform_comp.get_position();
    const auto rotation = transform_comp.get_rotation_euler();
    const auto scale    = transform_comp.get_scale();

    data.add(
        "transform",
        std::format(
            "{} {} {}\t{} {} {}\t{} {} {}", position.x, position.y, position.z, rotation.x,
            rotation.y, rotation.z, scale.x, scale.y, scale.z
        )
    );

    // Воксели в дереве больше не лежат: узел называет .voxm, а сам объём пишет
    // тот, кто владеет библиотекой. Узел без ссылки законен — её раздаёт первая
    // запись, и до неё он просто ни на что не ссылается.
    if (world_->has<model_component>(ent)) {
        const auto& source = world_->get<model_component>(ent).get_source();
        if (!source.empty()) {
            data.add("model", source.str());
        }
    }

    if (world_->has<animation_target_component>(ent)) {
        data.add("anim_target", world_->get<animation_target_component>(ent).get_name());
    }

    if (world_->has<socket_component>(ent)) {
        for (const auto& sp : world_->get<socket_component>(ent).get_sockets()) {
            const auto rot = math::quat_to_euler(sp.rotation);
            data.add("socket", sp.name)
                .set_prop("pos", detail::write_vec3f(sp.position))
                .set_prop("rot", detail::write_vec3f(rot))
                .set_prop("scale", detail::write_vec3f(sp.scale));
        }
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

    // Трансформ читается первым и запоминается: позу покоя цель анимации берёт
    // из него. Порядок этот содержательный, и на этапе 4 он станет явной фазой,
    // а не соседством строк.
    const auto node = detail::read_transform(data);
    if (node.has_value()) {
        world_->system<transform_system>()
            .modify(ent)
            .set_position(node->position)
            .set_rotation_euler(node->rotation)
            .set_scale(node->scale);
    }

    if (const auto* target = data.find("anim_target"); target != nullptr && !opts.skip_targets) {
        world_->modify(ent).with<animation_target_component>();
        auto target_mod = world_->system<animation_system>().modify_target(ent);
        target_mod.set_target_name(target->value);

        if (node.has_value()) {
            transform rest;
            rest.set_position(node->position);
            rest.set_rotation_euler(node->rotation);
            rest.set_scale(node->scale);
            target_mod.set_rest_transform(rest);
        }
    }

    if (!opts.skip_sockets) {
        attach_sockets_(data, ent);
    }

    attach_model_(data, ent);
}

auto vox_deserializer::attach_sockets_(const asset::vox_entity_data& data, entity ent) -> void {
    // Повтор тега — это список: сокеты идут подряд, и компонент заводится по
    // первому из них.
    bool attached = false;

    for (const auto& tag : data.tags) {
        if (tag.name != "socket") {
            continue;
        }

        if (!attached) {
            world_->modify(ent).with<socket_component>();
            attached = true;
        }

        world_->system<socket_system>().modify(ent).add_socket(
            tag.value, detail::read_vec3f(tag.prop("pos"), vec3f{}),
            math::euler_to_quat(detail::read_vec3f(tag.prop("rot"), vec3f{})),
            detail::read_vec3f(tag.prop("scale"), vec3f{1.0F, 1.0F, 1.0F})
        );
    }
}

auto vox_deserializer::attach_model_(const asset::vox_entity_data& data, entity ent) -> void {
    const auto source = data.value_of("model");
    if (source.empty()) {
        return;
    }

    const auto ref = asset::asset_ref{source};

    // Битая ссылка не повод ронять загрузку: узел встаёт без объёма, о чём
    // сказано в логе, и остальной префаб открывается целиком.
    auto loaded = library_->load(ref);
    if (!loaded.has_value()) {
        log::warn(
            detail::vox_deserializer_lc, "entity '{}' refers to a missing model '{}'", data.name,
            ref.str()
        );
        return;
    }

    world_->modify(ent).with<model_component>();
    world_->system<model_system>().modify(ent).set_model(*loaded, ref);
}

}  // namespace vw::ecs

