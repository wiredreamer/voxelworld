export module vw.world:scene.serial;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;
import :systems.variant;
import :scene.codecs;

export namespace vw::ecs {

class world;

class vox_serializer final {
public:
    using entity_names_type = std::unordered_map<entity, std::string>;
    using error_type        = asset::vox_writer::error_type;

    struct options {
        std::optional<entity_names_type> entity_names;
    };

    vox_serializer(
        world& world, asset::vox_writer& writer, entity root, options opts = {},
        const component_registry& codecs = default_components()
    );

    auto serialize(const std::filesystem::path& filepath) -> std::expected<void, error_type>;

    [[nodiscard]] auto extract() const -> asset::vox_prefab_data;

private:
    auto generate_entity_names_() -> void;
    [[nodiscard]] auto extract_entity_(entity ent) const -> asset::vox_entity_data;

    world* world_;
    asset::vox_writer* writer_;
    const component_registry* codecs_;
    entity root_;
    entity_names_type entity_names_;
};

class vox_deserializer final {
public:
    using error_type = asset::vox_parser::error_type;

    struct options {
        // Теги, которые в этот раз не применяются: превью сокета ставит чужой
        // префаб в сцену и не хочет ни его сокетов, ни его целей анимации.
        std::vector<std::string> skip_tags;
    };

    struct result {
        std::string root_name;
        std::unordered_map<std::string, entity> name_to_entity;
        std::unordered_map<entity, std::string> entity_to_name;
        std::vector<entity> entities;
    };

    vox_deserializer(
        world& world, asset::vox_parser& parser, asset::model_library& library,
        const component_registry& codecs = default_components()
    );

    auto deserialize(const std::filesystem::path& filepath) -> std::expected<result, error_type>;
    auto deserialize(const std::filesystem::path& filepath, const options& opts)
        -> std::expected<result, error_type>;

    // Разбор и применение разведены: данные префаба существуют помимо файла —
    // их отдаёт и разборщик потока, и тест, и подстановка варианта.
    [[nodiscard]] auto instantiate(const asset::vox_prefab_data& prefab, const options& opts)
        -> result;

    // Поставить в слот кандидата-поддерево. Здесь, а не в системе вариантов:
    // поддерево — это файл, а файл умеет читать только тот, у кого есть
    // разборщик. Отказ ничего не трогает — контракт проверяется до того, как
    // прежнее содержимое снято.
    auto put_variant(entity node, std::size_t index) -> std::expected<void, variant_error>;

private:
    auto clear_content_(entity node) -> void;
    auto create_entity_(const asset::vox_entity_data& data, result& res) -> void;
    auto link_parent_(const asset::vox_entity_data& data, result& res) -> void;
    auto attach_rig_(const asset::vox_prefab_data& prefab, const result& res) -> void;
    auto attach_machines_(const asset::vox_prefab_data& prefab, const result& res) -> void;

    world* world_;
    asset::vox_parser* parser_;
    asset::model_library* library_;
    const component_registry* codecs_;
};

}  // namespace vw::ecs
