module vw.testbed;

import std;
import vw.core;

namespace vw::testbed {
namespace {

struct scene_entry {
    std::string_view name;
    auto (*build)(testbed_app&, const arg_reader&) -> std::unique_ptr<scene>;
};

template <typename Scene>
constexpr auto entry_for(std::string_view name) -> scene_entry {
    return {name, [](testbed_app& stand, const arg_reader& args) -> std::unique_ptr<scene> {
                return std::make_unique<Scene>(stand, args);
            }};
}

const std::array<scene_entry, 8> scene_table{{
    entry_for<terrain_scene>("terrain"),
    entry_for<voxel_edits_scene>("voxel-edits"),
    entry_for<lamp_edits_scene>("lamp-edits"),
    entry_for<standing_lights_scene>("standing-lights"),
    entry_for<clustered_lights_scene>("clustered-lights"),
    entry_for<blob_shadows_scene>("blob-shadows"),
    entry_for<animated_crowd_scene>("animated-crowd"),
    entry_for<lod_probe_scene>("lod-probe"),
}};

}  // namespace

auto find_scene(
    std::string_view name, const arg_reader& args
) -> std::optional<scene_factory> {
    const auto found = std::ranges::find(scene_table, name, &scene_entry::name);
    if (found == scene_table.end()) {
        return std::nullopt;
    }

    return scene_factory{[build = found->build, args](testbed_app& stand) {
        return build(stand, args);
    }};
}

auto scene_names() -> std::vector<std::string_view> {
    std::vector<std::string_view> names;
    names.reserve(scene_table.size());
    for (const auto& entry : scene_table) {
        names.push_back(entry.name);
    }
    return names;
}

}  // namespace vw::testbed
