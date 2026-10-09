module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view prefab_extension  = ".vox";
constexpr std::string_view clip_extension    = ".voxa";
constexpr std::string_view machine_extension = ".voxf";

[[nodiscard]] auto refuse(
    std::string message
) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

[[nodiscard]] auto joined(
    const std::vector<std::string>& names
) -> std::string {
    std::string list;
    for (const std::string& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list;
}

[[nodiscard]] auto is_plain_stem(
    std::string_view text
) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char symbol) {
        const bool letter = (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z');
        const bool digit  = symbol >= '0' && symbol <= '9';
        return letter || digit || symbol == '_' || symbol == '-';
    });
}

[[nodiscard]] auto stem_of(
    std::string_view name, std::string_view extension
) -> std::string_view {
    if (name.ends_with(extension)) {
        name.remove_suffix(extension.size());
    }
    return name;
}

[[nodiscard]] auto files_naming(
    const fs::path& folder,
    std::string_view extension,
    std::string_view needle,
    const fs::path& except
) -> std::vector<std::string> {
    std::vector<std::string> users;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != extension ||
            entry.path() == except) {
            continue;
        }

        std::ifstream file{entry.path(), std::ios::binary};
        const std::string text{
            std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}
        };
        if (text.contains(needle)) {
            users.push_back(entry.path().filename().string());
        }
    }

    std::ranges::sort(users);
    return users;
}

}  // namespace

asset_service::asset_service(
    engine_type& eng, app_state& state
)
    : engine_(&eng), state_(&state) {}

auto asset_service::remove(
    asset_kind kind, std::string_view name
) -> std::expected<std::vector<std::string>, std::string> {
    switch (kind) {
        case asset_kind::prefab:
            return remove_prefab_(name);
        case asset_kind::clip:
            return remove_clip_(name);
        case asset_kind::machine:
            return remove_machine_(name);
    }
    return refuse("there is no such kind of asset");
}

auto asset_service::remove_prefab_(
    std::string_view name
) -> std::expected<std::vector<std::string>, std::string> {
    const std::string_view stem = stem_of(name, prefab_extension);
    if (!is_plain_stem(stem)) {
        return refuse(std::format("'{}' cannot name a prefab file", name));
    }

    const std::string filename = std::format("{}{}", stem, prefab_extension);
    const fs::path file        = app_state::prefab_dir() / filename;

    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return refuse(
            std::format(
                "there is no prefab '{}'; assets_list names the prefabs there are", filename
            )
        );
    }
    if (state_->file.filename == filename) {
        return refuse(std::format("the prefab '{}' is open; close it first", filename));
    }

    const std::string as_candidate = std::format("{}/{}", asset::dirs::prefabs, filename);
    const std::string its_volumes  = std::format("{}/{}/", asset::dirs::models, stem);

    std::vector<std::string> users =
        files_naming(app_state::prefab_dir(), prefab_extension, as_candidate, file);
    for (std::string& user :
         files_naming(app_state::prefab_dir(), prefab_extension, its_volumes, file)) {
        if (!std::ranges::contains(users, user)) {
            users.push_back(std::move(user));
        }
    }
    if (!users.empty()) {
        return refuse(
            std::format(
                "the prefab '{}' or its volumes are used by: {}; change them first",
                filename,
                joined(users)
            )
        );
    }

    std::vector<std::string> removed;

    if (!fs::remove(file, ec) || ec) {
        return refuse(std::format("the file '{}' could not be removed", filename));
    }
    removed.push_back(as_candidate);

    const fs::path volumes = app_state::model_dir() / fs::path{stem};
    if (fs::is_directory(volumes, ec)) {
        for (const auto& entry : fs::directory_iterator(volumes, ec)) {
            removed.push_back(std::format("{}{}", its_volumes, entry.path().filename().string()));
        }
        fs::remove_all(volumes, ec);
    }

    std::ranges::sort(removed);
    return removed;
}

auto asset_service::remove_clip_(
    std::string_view name
) -> std::expected<std::vector<std::string>, std::string> {
    const std::string_view stem = stem_of(name, clip_extension);
    if (!is_plain_stem(stem)) {
        return refuse(std::format("'{}' cannot name a clip file", name));
    }

    const std::string filename = std::format("{}{}", stem, clip_extension);
    const fs::path file        = app_state::clip_dir() / filename;

    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return refuse(
            std::format("there is no clip '{}'; assets_list names the clips there are", filename)
        );
    }
    if (engine_->get_world().resource<asset::animation_clip_registry>().has(stem)) {
        return refuse(std::format("the clip '{}' is open; close it with clip_close first", stem));
    }

    const std::string as_state_clip = std::format("{}/{}", asset::dirs::animations, filename);
    const auto users =
        files_naming(app_state::fsm_dir(), machine_extension, as_state_clip, fs::path{});
    if (!users.empty()) {
        return refuse(
            std::format(
                "the clip '{}' is played by the state machines: {}; change them first",
                stem,
                joined(users)
            )
        );
    }

    if (!fs::remove(file, ec) || ec) {
        return refuse(std::format("the file '{}' could not be removed", filename));
    }
    return std::vector<std::string>{as_state_clip};
}

auto asset_service::remove_machine_(
    std::string_view name
) -> std::expected<std::vector<std::string>, std::string> {
    const std::string_view stem = stem_of(name, machine_extension);
    if (!is_plain_stem(stem)) {
        return refuse(std::format("'{}' cannot name a state machine file", name));
    }

    const std::string filename = std::format("{}{}", stem, machine_extension);
    const fs::path file        = app_state::fsm_dir() / filename;

    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return refuse(
            std::format(
                "there is no state machine '{}'; assets_list names the machines there are", filename
            )
        );
    }

    const std::string as_layer = std::format("{}/{}", asset::dirs::fsm, filename);

    const auto root = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root != state_->scene.name_to_entity.end()) {
        auto& world = engine_->get_world();
        if (world.has<ecs::animation_machines_component>(root->second)) {
            for (const asset::asset_ref& attached :
                 world.get<ecs::animation_machines_component>(root->second).get_sources()) {
                if (attached.str() == as_layer) {
                    return refuse(
                        std::format(
                            "the open prefab runs the state machine '{}'; detach it with "
                            "prefab_set_machines first",
                            stem
                        )
                    );
                }
            }
        }
    }

    const auto users =
        files_naming(app_state::prefab_dir(), prefab_extension, as_layer, fs::path{});
    if (!users.empty()) {
        return refuse(
            std::format(
                "the state machine '{}' is run by the prefabs: {}; detach it there first",
                stem,
                joined(users)
            )
        );
    }

    if (!fs::remove(file, ec) || ec) {
        return refuse(std::format("the file '{}' could not be removed", filename));
    }
    if (state_->fsm.is_open() && state_->fsm.source.str() == as_layer) {
        state_->fsm = fsm_document{};
    }
    return std::vector<std::string>{as_layer};
}

}  // namespace vw::sculptor
