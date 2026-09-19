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

class asset_storage final {
public:
    asset_storage(vox_parser& parser, model_library& library);

    auto load_prefab(std::string_view name, const asset_ref& ref) -> void;

    [[nodiscard]] auto get_prefab(std::string_view name) const -> const vox_prefab_data*;

    [[nodiscard]] auto get_entity(std::string_view prefab, std::string_view entity_name) const
        -> const vox_entity_data&;

    [[nodiscard]] auto get_model(std::string_view prefab, std::string_view entity_name) const
        -> std::shared_ptr<model>;

    [[nodiscard]] auto library() const -> model_library& {
        return *library_;
    }

    auto load_clip(const asset_ref& ref) -> std::shared_ptr<animation_clip>;
    [[nodiscard]] auto get_clip(const asset_ref& ref) const -> std::shared_ptr<animation_clip>;

    auto load_fsm(const asset_ref& ref) -> void;
    [[nodiscard]] auto get_fsm(const asset_ref& ref) const -> const voxf_data*;

    [[nodiscard]] auto make_fsm(const voxf_data& data) const -> animation_fsm;

private:
    vox_parser* parser_;
    model_library* library_;
    string_map<vox_prefab_data> prefabs_;
    std::unordered_map<asset_ref, std::shared_ptr<animation_clip>> clips_;
    std::unordered_map<asset_ref, voxf_data> machines_;
};

}  // namespace vw::asset
