#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

auto prefab(std::string_view name) -> asset::vox_prefab_data {
    asset::vox_parser_plain parser;
    const auto parsed =
        parser.parse(std::filesystem::path{VW_ASSET_DIR} / "prefabs" / std::format("{}.vox", name));
    REQUIRE(parsed.has_value());
    return *parsed;
}

auto without_models(asset::vox_prefab_data data) -> asset::vox_prefab_data {
    for (auto& entity : data.entities) {
        std::erase_if(entity.tags, [](const asset::vox_tag& tag) { return tag.name == "model"; });
    }
    return data;
}

}  // namespace

// см. docs/ENGINE.md#существа-на-одном-риге
TEST_CASE("the skeleton is the humanoid rig with other models and nothing else", "[asset][rig]") {
    const auto human    = prefab("p_humanoid");
    const auto skeleton = prefab("p_skeleton");

    REQUIRE(skeleton.rig == "humanoid");
    REQUIRE(without_models(skeleton) == without_models(human));

    for (const auto& entity : skeleton.entities) {
        const auto model = entity.value_of("model");
        if (!model.empty()) {
            INFO(entity.name);
            REQUIRE(model.starts_with("models/p_skeleton/"));
            REQUIRE(std::filesystem::exists(std::filesystem::path{VW_ASSET_DIR} / model));
        }
    }
}

TEST_CASE("every humanoid clip drives only parts the skeleton has", "[asset][rig]") {
    const auto skeleton = prefab("p_skeleton");
    std::set<std::string, std::less<>> targets;
    for (const auto& entity : skeleton.entities) {
        if (const auto target = entity.value_of("anim_target"); !target.empty()) {
            targets.emplace(target);
        }
    }
    REQUIRE(targets.size() == 6);

    int32 clips = 0;
    for (const auto& file :
         std::filesystem::directory_iterator{std::filesystem::path{VW_ASSET_DIR} / "animations"}) {
        if (!file.path().filename().string().starts_with("a_humanoid_")) {
            continue;
        }
        INFO(file.path().filename().string());
        asset::voxa_deserializer reader;
        const auto clip = reader.deserialize(file.path());
        REQUIRE(clip.has_value());
        REQUIRE((*clip)->get_rig() == "humanoid");
        for (const auto& track : (*clip)->get_tracks()) {
            INFO(track.get_target_name());
            REQUIRE(targets.contains(track.get_target_name()));
        }
        ++clips;
    }
    REQUIRE(clips > 30);
}
