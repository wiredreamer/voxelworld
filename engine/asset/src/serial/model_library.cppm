export module vw.asset:serial.library;

import std;

import vw.core;
import :model;
import :serial.ref;
import :serial.voxm;

export namespace vw::asset {

class model_library final {
public:
    using load_error = voxm_deserializer::error_type;
    using save_error = voxm_serializer::error_type;

    model_library(
        model_registry& registry, const voxel_registry& voxel_types, std::filesystem::path root
    );

    [[nodiscard]] auto load(const asset_ref& ref)
        -> std::expected<std::shared_ptr<model>, load_error>;

    [[nodiscard]] auto save(const asset_ref& ref, const model& volume)
        -> std::expected<void, save_error>;

    auto adopt(const asset_ref& ref, std::shared_ptr<model> volume) -> void;

    auto forget(const asset_ref& ref) -> void;

    [[nodiscard]] auto find(const asset_ref& ref) const -> std::shared_ptr<model>;

    [[nodiscard]] auto path_of(const asset_ref& ref) const -> std::filesystem::path;

    [[nodiscard]] auto root() const -> const std::filesystem::path& {
        return root_;
    }

    [[nodiscard]] auto registry() const -> model_registry& {
        return *registry_;
    }

private:
    model_registry* registry_;
    const voxel_registry* voxel_types_;
    std::filesystem::path root_;
    std::unordered_map<asset_ref, std::shared_ptr<model>> loaded_;
};

}  // namespace vw::asset
