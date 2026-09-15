export module vw.asset:serial.storage;

import std;

import vw.core;
import :model;
import :anim;
import :serial.ref;
import :serial.library;
import :serial.vox;
import :serial.voxa;
import :serial.voxf;

export namespace vw::asset {

// Хранит загруженные префабы (модели с метаданными), клипы анимаций и описания
// автоматов.
class asset_storage final {
public:
    asset_storage(vox_parser& parser, model_library& library);

    // Вместе с префабом приходит всё, на что он ссылается: объёмы узлов,
    // автоматы из шапки и клипы этих автоматов. Битая ссылка загрузку не рвёт.
    auto load_prefab(std::string_view name, const asset_ref& ref) -> void;

    [[nodiscard]] auto get_prefab(std::string_view name) const -> const vox_prefab_data*;

    [[nodiscard]] auto get_entity(std::string_view prefab, std::string_view entity_name) const
        -> const vox_entity_data&;

    [[nodiscard]] auto get_model(std::string_view prefab, std::string_view entity_name) const
        -> std::shared_ptr<model>;

    // Библиотека объёмов наружу: тот, кто ставит узел префаба в мир, грузит
    // его объём через неё же, а не заводит вторую копию рядом.
    [[nodiscard]] auto library() const -> model_library& {
        return *library_;
    }

    // Ключ — ссылка, а не короткое имя: второго имени у файла нет, и придуманное
    // пришлось бы держать в согласии с тем, что написано в автомате.
    auto load_clip(const asset_ref& ref) -> std::shared_ptr<animation_clip>;
    [[nodiscard]] auto get_clip(const asset_ref& ref) const -> std::shared_ptr<animation_clip>;

    auto load_fsm(const asset_ref& ref) -> void;
    [[nodiscard]] auto get_fsm(const asset_ref& ref) const -> const voxf_data*;

    // Свежий автомат по описанию: у каждого существа он свой, потому что в нём
    // лежит текущее состояние. Клипы при этом общие — их держит хранилище.
    [[nodiscard]] auto make_fsm(const voxf_data& data) const -> animation_fsm;

private:
    vox_parser* parser_;
    model_library* library_;
    std::unordered_map<std::string, vox_prefab_data> prefabs_;
    std::unordered_map<asset_ref, std::shared_ptr<animation_clip>> clips_;
    std::unordered_map<asset_ref, voxf_data> machines_;
};

}  // namespace vw::asset
