module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace {
constexpr log::log_category lc_library{"model_library"};
}  // namespace

model_library::model_library(
    model_registry& registry, const voxel_registry& voxel_types, std::filesystem::path root
)
    : registry_(&registry), voxel_types_(&voxel_types), root_(std::move(root)) {}

auto model_library::load(
    const asset_ref& ref
) -> std::expected<std::shared_ptr<model>, load_error> {
    if (ref.empty()) {
        return std::unexpected(load_error::file_open_failed);
    }

    if (const auto it = loaded_.find(ref); it != loaded_.end()) {
        return it->second;
    }

    voxm_deserializer deserializer{*registry_, *voxel_types_};
    auto result = deserializer.deserialize(path_of(ref));
    if (!result.has_value()) {
        log::warn(lc_library, "failed to load model '{}'", ref.str());
        return std::unexpected(result.error());
    }

    loaded_[ref] = *result;
    return *result;
}

auto model_library::save(
    const asset_ref& ref, const model& volume
) -> std::expected<void, save_error> {
    const auto filepath = path_of(ref);

    std::error_code ec;
    std::filesystem::create_directories(filepath.parent_path(), ec);
    if (ec) {
        log::warn(
            lc_library, "failed to create directory for '{}': {}", ref.str(), ec.message()
        );
        return std::unexpected(save_error::file_open_failed);
    }

    voxm_serializer serializer{volume};
    return serializer.serialize(filepath);
}

auto model_library::adopt(
    const asset_ref& ref, std::shared_ptr<model> volume
) -> void {
    if (ref.empty()) {
        return;
    }
    loaded_[ref] = std::move(volume);
}

auto model_library::find(
    const asset_ref& ref
) const -> std::shared_ptr<model> {
    const auto it = loaded_.find(ref);
    return it != loaded_.end() ? it->second : nullptr;
}

auto model_library::path_of(
    const asset_ref& ref
) const -> std::filesystem::path {
    return root_ / std::filesystem::path{ref.str()};
}

}  // namespace vw::asset
