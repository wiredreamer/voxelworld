module vw.world;

import std;

import vw.core;
import vw.asset;
import vw.ecs;

namespace vw::ecs {

namespace detail {
constexpr log::log_category component_codec_lc{"component_codec"};
}  // namespace detail

namespace {

// Значение тега — строка, и сколько в ней чисел, знает только читающий. Здесь
// эти две стороны и встречаются: формат хранит текст, смысл ему придаёт кодек.
auto write_vec3f(const vec3f& value) -> std::string {
    return std::format("{} {} {}", value.x, value.y, value.z);
}

auto read_vec3f(std::string_view text, vec3f fallback) -> vec3f {
    std::array<float32, 3> values{};
    return asset::parse_floats(text, values) ? vec3f{values[0], values[1], values[2]} : fallback;
}

auto register_transform(component_registry& codecs) -> void {
    component_codec codec;
    codec.tag   = "transform";
    codec.phase = apply_phase::transform;

    codec.read = [](const component_read& in) {
        std::array<float32, 9> values{};
        if (!asset::parse_floats(in.tags.front()->value, values)) {
            log::warn(
                detail::component_codec_lc, "node '{}' has a malformed transform", in.node_name
            );
            return;
        }

        in.target.system<transform_system>()
            .modify(in.ent)
            .set_position(vec3f{values[0], values[1], values[2]})
            .set_rotation_euler(vec3f{values[3], values[4], values[5]})
            .set_scale(vec3f{values[6], values[7], values[8]});
    };

    codec.write = [](const component_write& out) {
        const auto& transform_comp = out.source.get<transform_component>(out.ent);

        const auto position = transform_comp.get_position();
        const auto rotation = transform_comp.get_rotation_euler();
        const auto scale    = transform_comp.get_scale();

        out.out.add(
            "transform",
            std::format(
                "{} {} {}\t{} {} {}\t{} {} {}", position.x, position.y, position.z, rotation.x,
                rotation.y, rotation.z, scale.x, scale.y, scale.z
            )
        );
    };

    codecs.register_for<transform_component>(std::move(codec));
}

auto register_model(component_registry& codecs) -> void {
    component_codec codec;
    codec.tag = "model";

    codec.read = [](const component_read& in) {
        const auto ref = asset::asset_ref{in.tags.front()->value};

        // Битая ссылка не повод ронять загрузку: узел встаёт без объёма, о чём
        // сказано в логе, и остальной префаб открывается целиком.
        auto loaded = in.library.load(ref);
        if (!loaded.has_value()) {
            log::warn(
                detail::component_codec_lc, "node '{}' refers to a missing model '{}'",
                in.node_name, ref.str()
            );
            return;
        }

        in.target.modify(in.ent).with<model_component>();
        in.target.system<model_system>().modify(in.ent).set_model(*loaded, ref);
    };

    codec.write = [](const component_write& out) {
        // Узел без ссылки законен: её раздаёт первая запись объёма, и до неё он
        // просто ни на что не ссылается.
        const auto& source = out.source.get<model_component>(out.ent).get_source();
        if (!source.empty()) {
            out.out.add("model", source.str());
        }
    };

    codecs.register_for<model_component>(std::move(codec));
}

auto register_anim_target(component_registry& codecs) -> void {
    component_codec codec;
    codec.tag = "anim_target";

    codec.read = [](const component_read& in) {
        in.target.modify(in.ent).with<animation_target_component>();

        auto target_mod = in.target.system<animation_system>().modify_target(in.ent);
        target_mod.set_target_name(in.tags.front()->value);

        // Поза покоя берётся из мира, а не из файла: трансформ к этому моменту
        // уже применён — на то и фаза.
        if (in.target.has<transform_component>(in.ent)) {
            target_mod.set_rest_transform(
                in.target.get<transform_component>(in.ent).get_transform()
            );
        }
    };

    codec.write = [](const component_write& out) {
        out.out.add(
            "anim_target", out.source.get<animation_target_component>(out.ent).get_name()
        );
    };

    codecs.register_for<animation_target_component>(std::move(codec));
}

auto register_variant(component_registry& codecs) -> void {
    component_codec codec;
    codec.tag = "variant";

    codec.read = [](const component_read& in) {
        in.target.modify(in.ent).with<variant_slot_component>();

        const auto* tag = in.tags.front();
        auto slot       = in.target.system<variant_system>().modify(in.ent);
        slot.set_name(std::string{tag->value});

        std::vector<asset::asset_ref> candidates;
        std::vector<std::string> targets;
        std::vector<std::string> sockets;
        std::size_t selected = 0;

        // Ключи повторяются: кандидатов у слота несколько, и требований тоже.
        // prop() вернул бы только первый, поэтому свойства читаются подряд.
        for (const auto& [key, value] : tag->props) {
            if (key == "candidate") {
                candidates.emplace_back(value);
            } else if (key == "selected") {
                std::from_chars(value.data(), value.data() + value.size(), selected);
            } else if (key == "target") {
                targets.push_back(value);
            } else if (key == "socket") {
                sockets.push_back(value);
            }
        }

        slot.set_candidates(std::move(candidates));
        slot.set_required_targets(std::move(targets));
        slot.set_required_sockets(std::move(sockets));
        slot.select(selected);
    };

    codec.write = [](const component_write& out) {
        const auto& slot = out.source.get<variant_slot_component>(out.ent);

        auto& tag = out.out.add("variant", slot.get_name());
        for (const auto& ref : slot.get_candidates()) {
            tag.set_prop("candidate", ref.str());
        }

        tag.set_prop("selected", std::format("{}", slot.get_selected()));

        for (const auto& target : slot.required_targets()) {
            tag.set_prop("target", target);
        }

        for (const auto& socket : slot.required_sockets()) {
            tag.set_prop("socket", socket);
        }
    };

    codecs.register_for<variant_slot_component>(std::move(codec));
}

auto register_socket(component_registry& codecs) -> void {
    component_codec codec;
    codec.tag = "socket";

    codec.read = [](const component_read& in) {
        in.target.modify(in.ent).with<socket_component>();

        auto& socket_sys = in.target.system<socket_system>();
        for (const auto* tag : in.tags) {
            socket_sys.modify(in.ent).add_socket(
                tag->value, read_vec3f(tag->prop("pos"), vec3f{}),
                math::euler_to_quat(read_vec3f(tag->prop("rot"), vec3f{})),
                read_vec3f(tag->prop("scale"), vec3f{1.0F, 1.0F, 1.0F})
            );
        }
    };

    codec.write = [](const component_write& out) {
        for (const auto& sp : out.source.get<socket_component>(out.ent).get_sockets()) {
            const auto rotation = math::quat_to_euler(sp.rotation);

            out.out.add("socket", sp.name)
                .set_prop("pos", write_vec3f(sp.position))
                .set_prop("rot", write_vec3f(rotation))
                .set_prop("scale", write_vec3f(sp.scale));
        }
    };

    codecs.register_for<socket_component>(std::move(codec));
}

}  // namespace

component_registry::component_registry() {
    // Порядок регистрации — он же порядок тегов в файле. Читателю он безразличен,
    // порядок применения задают фазы, а вот диффы от него остаются спокойными.
    register_transform(*this);
    register_model(*this);
    register_anim_target(*this);
    register_socket(*this);
    register_variant(*this);
}

auto component_registry::find(
    std::string_view tag
) const -> const component_codec* {
    const auto it = std::ranges::find(codecs_, tag, &component_codec::tag);
    return it != codecs_.end() ? &(*it) : nullptr;
}

auto has_component(
    world& target, entity ent, uint32 component
) -> bool {
    const auto* pool = target.registry().try_pool(component);
    return pool != nullptr && pool->has(ent);
}

auto default_components() -> component_registry& {
    static component_registry registry;
    return registry;
}

auto apply_node_phase(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs, apply_phase phase, std::span<const std::string> skip_tags
) -> void {
    std::vector<const asset::vox_tag*> tags;

    for (const auto& codec : codecs.all()) {
        if (codec.phase != phase) {
            continue;
        }

        if (std::ranges::find(skip_tags, codec.tag) != skip_tags.end()) {
            continue;
        }

        tags.clear();
        for (const auto& tag : data.tags) {
            if (tag.name == codec.tag) {
                tags.push_back(&tag);
            }
        }

        if (tags.empty()) {
            continue;
        }

        codec.read(component_read{
            .target    = target,
            .ent       = ent,
            .library   = library,
            .node_name = data.name,
            .tags      = tags,
        });
    }
}

auto apply_node(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs, std::span<const std::string> skip_tags
) -> void {
    for (const auto phase : {apply_phase::transform, apply_phase::general}) {
        apply_node_phase(target, ent, data, library, codecs, phase, skip_tags);
    }
}

auto extract_node(
    world& source, entity ent, asset::vox_entity_data& out, const component_registry& codecs
) -> void {
    for (const auto& codec : codecs.all()) {
        if (!has_component(source, ent, codec.component)) {
            continue;
        }

        codec.write(component_write{.source = source, .ent = ent, .out = out});
    }
}

}  // namespace vw::ecs
