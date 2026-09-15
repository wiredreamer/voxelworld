export module vw.asset:serial.storage;

import std;

import vw.core;
import :model;
import :anim;
import :serial.ref;
import :serial.library;
import :serial.vox;
import :serial.voxa;

export namespace vw::asset {

// Хранит загруженные префабы (модели с метаданными) и клипы анимаций.
class asset_storage final {
public:
    asset_storage(vox_parser& parser, model_library& library);

    auto load_prefab(std::string_view name, const asset_ref& ref) -> void;
    auto load_clip(std::string_view name, const std::filesystem::path& filepath) -> void;

    [[nodiscard]] auto get_entity(std::string_view prefab, std::string_view entity_name) const
        -> const vox_entity_data&;

    [[nodiscard]] auto get_model(std::string_view prefab, std::string_view entity_name) const
        -> std::shared_ptr<model>;

    // Библиотека объёмов наружу: тот, кто ставит узел префаба в мир, грузит
    // его объём через неё же, а не заводит вторую копию рядом.
    [[nodiscard]] auto library() const -> model_library& {
        return *library_;
    }

    [[nodiscard]] auto get_clip(std::string_view name) const -> std::shared_ptr<animation_clip>;
    [[nodiscard]] auto has_clip(std::string_view name) const -> bool;

private:
    vox_parser* parser_;
    model_library* library_;
    std::unordered_map<std::string, vox_prefab_data> prefabs_;
    std::unordered_map<std::string, std::shared_ptr<animation_clip>> clips_;
};

}  // namespace vw::asset
