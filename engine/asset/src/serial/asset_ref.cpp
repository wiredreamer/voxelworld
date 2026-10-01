module vw.asset;

import std;
import vw.core;

namespace vw::asset {

asset_ref::asset_ref(
    std::string_view path
)
    : path_(path) {
    std::ranges::replace(path_, '\\', '/');

    std::size_t start = 0;
    while (start < path_.size()) {
        if (path_.compare(start, 2, "./") == 0) {
            start += 2;
        } else if (path_[start] == '/') {
            start += 1;
        } else {
            break;
        }
    }
    path_.erase(0, start);
}

auto asset_ref::extension() const -> std::string_view {
    const std::string_view text{path_};
    const auto slash = text.find_last_of('/');
    const auto begin = slash == std::string_view::npos ? std::size_t{0} : slash + 1;
    const auto dot   = text.find_last_of('.');

    if (dot == std::string_view::npos || dot <= begin) {
        return {};
    }
    return text.substr(dot);
}

auto asset_ref::stem() const -> std::string_view {
    const std::string_view text{path_};
    const auto slash = text.find_last_of('/');
    const auto begin = slash == std::string_view::npos ? std::size_t{0} : slash + 1;
    const auto name  = text.substr(begin);

    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return name;
    }
    return name.substr(0, dot);
}

auto default_model_ref(
    const asset_ref& prefab, std::string_view entity_name
) -> asset_ref {
    return asset_ref{std::format("{}/{}/{}.voxm", dirs::models, prefab.stem(), entity_name)};
}

auto rehomed_model_ref(
    const asset_ref& model, const asset_ref& from_prefab, const asset_ref& to_prefab
) -> std::optional<asset_ref> {
    if (from_prefab.stem().empty() || to_prefab.stem().empty()) {
        return std::nullopt;
    }

    const auto home = std::format("{}/{}/", dirs::models, from_prefab.stem());
    const std::string_view path{model.str()};
    if (!path.starts_with(home)) {
        return std::nullopt;
    }

    return asset_ref{
        std::format("{}/{}/{}", dirs::models, to_prefab.stem(), path.substr(home.size()))
    };
}

auto renamed_model_ref(
    const asset_ref& model, std::string_view stem
) -> std::optional<asset_ref> {
    constexpr std::string_view forbidden_characters = "/\\:*?\"<>|.";

    if (model.empty() || stem.empty() ||
        stem.find_first_of(forbidden_characters) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view path{model.str()};
    const auto file_name_size = model.stem().size() + model.extension().size();
    const auto folder         = path.substr(0, path.size() - file_name_size);

    return asset_ref{std::format("{}{}{}", folder, stem, model.extension())};
}

}  // namespace vw::asset
