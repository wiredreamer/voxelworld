export module vw.world:scene.codecs;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;

export namespace vw::ecs {

class world;

enum class apply_phase : uint8 { transform, general };

struct component_read {
    world& target;
    entity ent;
    asset::model_library& library;

    std::string_view node_name;

    std::span<const asset::vox_tag* const> tags;
};

struct component_write {
    world& source;
    entity ent;
    asset::vox_entity_data& out;
};

struct component_codec {
    std::string tag;
    apply_phase phase = apply_phase::general;

    std::function<void(const component_read&)> read;
    std::function<void(const component_write&)> write;

    uint32 component = 0;
};

class component_registry final {
public:
    component_registry();

    template <typename T>
    auto register_for(component_codec codec) -> void {
        codec.component = component_id_of<T>();
        codecs_.push_back(std::move(codec));
    }

    [[nodiscard]] auto all() const -> std::span<const component_codec> {
        return codecs_;
    }

    [[nodiscard]] auto find(std::string_view tag) const -> const component_codec*;

private:
    std::vector<component_codec> codecs_;
};

[[nodiscard]] auto has_component(world& target, entity ent, uint32 component) -> bool;

[[nodiscard]] auto default_components() -> component_registry&;

auto apply_node_phase(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs, apply_phase phase, std::span<const std::string> skip_tags = {}
) -> void;

auto apply_node(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs = default_components(),
    std::span<const std::string> skip_tags = {}
) -> void;

auto extract_node(
    world& source, entity ent, asset::vox_entity_data& out,
    const component_registry& codecs = default_components()
) -> void;

}  // namespace vw::ecs
