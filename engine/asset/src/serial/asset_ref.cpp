module vw.asset;

import std;
import vw.core;

namespace vw::asset {

asset_ref::asset_ref(
    std::string_view path
)
    : path_(path) {
    std::ranges::replace(path_, '\\', '/');

    // Ведущие «./» и «/» стираются при сборке: ключом кеша служит сама ссылка, и
    // два написания одного пути обязаны дать одну запись, а не две модели.
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
    std::string_view dir = prefab.str();
    const auto slash     = dir.find_last_of('/');
    const auto dot       = dir.find_last_of('.');
    if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash + 1)) {
        dir = dir.substr(0, dot);
    }

    return asset_ref{std::format("{}/{}.voxm", dir, entity_name)};
}

}  // namespace vw::asset
