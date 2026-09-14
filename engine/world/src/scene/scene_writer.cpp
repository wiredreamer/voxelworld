module vw.world;


import std;
import vw.core;
import vw.asset;


namespace vw::ecs {

namespace detail {
constexpr log::log_category vox_writer_plain_lc{"vox_writer_plain"};
}  // namespace detail

auto vox_writer_plain::write(
    const std::filesystem::path& filepath, const vw::asset::vox_prefab_data& prefab
) -> std::expected<void, error_type> {
    std::ofstream file(filepath.string(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn(detail::vox_writer_plain_lc, "failed to open file for writing: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    write_header_(file, prefab);

    for (const auto& ent : prefab.entities) {
        write_entity_(file, ent);
    }

    if (!file.good()) {
        log::warn(detail::vox_writer_plain_lc, "write error for file: {}", filepath.string());
        return std::unexpected(error_type::write_failed);
    }

    return {};
}

auto vox_writer_plain::write_header_(
    std::ofstream& file, const vw::asset::vox_prefab_data& prefab
) -> void {
    file << std::format("# Vox File Version {}\n", asset::vox_file_version);
    file << std::format("root {}\n", prefab.root_name);
}

auto vox_writer_plain::write_entity_(
    std::ofstream& file, const vw::asset::vox_entity_data& ent
) -> void {
    file << std::format("entity {}\n", ent.name);

    if (!ent.parent_name.empty()) {
        file << std::format("\tparent {}\n", ent.parent_name);
    }

    if (ent.has_transform) {
        file << std::format(
            "\ttransform {} {} {}\t{} {} {}\t{} {} {}\n",
            ent.position.x, ent.position.y, ent.position.z,
            ent.rotation.x, ent.rotation.y, ent.rotation.z,
            ent.scale.x, ent.scale.y, ent.scale.z
        );
    }

    if (!ent.model.empty()) {
        file << std::format("\tmodel {}\n", ent.model.str());
    }

    if (ent.animation_target_name.has_value()) {
        file << std::format("\tanim_target {}\n", *ent.animation_target_name);
    }

    if (ent.has_sockets) {
        file << "\tsockets\n";
        for (const auto& sp : ent.sockets) {
            file << std::format(
                "\t\tsocket {} {} {} {} {} {} {} {} {} {}\n",
                sp.name,
                sp.position.x, sp.position.y, sp.position.z,
                sp.rotation.x, sp.rotation.y, sp.rotation.z,
                sp.scale.x, sp.scale.y, sp.scale.z
            );
        }
    }
}

}  // namespace vw::ecs

