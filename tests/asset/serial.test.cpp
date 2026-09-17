#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

auto parse_vox(std::string_view text) {
    std::istringstream input{std::string{text}};
    asset::vox_parser_plain parser;
    return parser.parse(input);
}

auto parse_voxa(std::string_view text) {
    std::istringstream input{std::string{text}};
    asset::voxa_deserializer deserializer;
    return deserializer.deserialize(input);
}

}  // namespace

TEST_CASE("the vox parser reads a prefab out of a stream", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "root root\n"
        "entity root\n"
        "\ttransform 0 0 0\t-0 -0 -0\t1 1 1\n"
        "entity hand_right\n"
        "\tparent root\n"
        "\ttransform 10 2 0\t1.5707963 -0 -0\t1 1 1\n"
        "\tmodel models/m_human/hand_right.voxm\n"
        "\tanim_target hand_right\n"
        "\tsocket hand_right\n"
        "\t\tpos 0 0 0\n"
        "\t\trot -1.570796 -0 -0\n"
        "\t\tscale 0.99 0.99 0.99\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->root_name == "root");
    REQUIRE(prefab->rig == "humanoid");
    REQUIRE(prefab->entities.size() == 2);

    const auto& root = prefab->entities.front();
    REQUIRE(root.value_of("transform") == "0 0 0\t-0 -0 -0\t1 1 1");
    REQUIRE(root.find("model") == nullptr);

    const auto& hand = prefab->entities.back();
    REQUIRE(hand.parent_name == "root");
    REQUIRE(hand.value_of("transform") == "10 2 0\t1.5707963 -0 -0\t1 1 1");
    REQUIRE(hand.value_of("model") == "models/m_human/hand_right.voxm");
    REQUIRE(hand.value_of("anim_target") == "hand_right");

    const auto* socket = hand.find("socket");
    REQUIRE(socket != nullptr);
    REQUIRE(socket->value == "hand_right");
    REQUIRE(socket->prop("pos") == "0 0 0");
    REQUIRE(socket->prop("scale") == "0.99 0.99 0.99");
}

TEST_CASE("an unknown tag is carried through untouched", "[serial]") {
    const auto first = parse_vox(
        "root body\n"
        "entity body\n"
        "\tlight point\n"
        "\t\tcolor 255 200 120\n"
        "\t\trange 12\n"
    );

    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);

    const auto* light = second->entities.front().find("light");
    REQUIRE(light != nullptr);
    REQUIRE(light->value == "point");
    REQUIRE(light->prop("range") == "12");
}

TEST_CASE("a repeated tag is a list", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tsocket hand_right\n"
        "\t\tpos 1 0 0\n"
        "\tsocket hand_left\n"
        "\t\tpos -1 0 0\n"
    );

    REQUIRE(prefab.has_value());

    const auto& tags = prefab->entities.front().tags;
    REQUIRE(tags.size() == 2);
    REQUIRE(tags.front().value == "hand_right");
    REQUIRE(tags.front().prop("pos") == "1 0 0");
    REQUIRE(tags.back().value == "hand_left");
    REQUIRE(tags.back().prop("pos") == "-1 0 0");
}

TEST_CASE("an unknown vox command is skipped, not fatal", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tqqq 1 2 3\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

TEST_CASE("a property without a tag is a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\t\tcolor 1 2 3\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::parse_error);
}

TEST_CASE("a truncated transform is not a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\ttransform 1 2\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.front().value_of("transform") == "1 2");

    std::array<float32, 9> values{};
    REQUIRE_FALSE(asset::parse_floats("1 2", values));
}

TEST_CASE("an empty vox stream yields an empty prefab", "[serial]") {
    const auto prefab = parse_vox("");

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.empty());
}

TEST_CASE("a prefab survives a write and a read", "[serial]") {
    constexpr std::string_view source =
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "root root\n"
        "entity root\n"
        "\ttransform 0 0 0\t-0 -0 -0\t1 1 1\n"
        "entity hand_right\n"
        "\tparent root\n"
        "\ttransform 10 2 0\t1.5707963 -0 -0\t1 1 1\n"
        "\tmodel models/m_human/hand_right.voxm\n"
        "\tanim_target hand_right\n"
        "\tsocket hand_right\n"
        "\t\tpos 0 0 0\n"
        "\t\trot -1.570796 -0 -0\n"
        "\t\tscale 0.99 0.99 0.99\n";

    const auto first = parse_vox(source);
    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);
}

TEST_CASE("a prefab names its state machines in layer order", "[serial]") {
    constexpr std::string_view source =
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "fsm fsm/humanoid_locomotion.voxf\n"
        "fsm fsm/humanoid_action.voxf\n"
        "root root\n"
        "entity root\n";

    const auto first = parse_vox(source);
    REQUIRE(first.has_value());
    REQUIRE(first->fsm_refs.size() == 2);
    REQUIRE(first->fsm_refs[0] == asset::asset_ref{"fsm/humanoid_locomotion.voxf"});
    REQUIRE(first->fsm_refs[1] == asset::asset_ref{"fsm/humanoid_action.voxf"});

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(second->fsm_refs == first->fsm_refs);
}

TEST_CASE("a bare node survives a write and a read", "[serial]") {
    const auto first = parse_vox(
        "# Vox File Version 4.0\n"
        "root root\n"
        "entity root\n"
    );
    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);
}

TEST_CASE("the voxa parser reads a clip out of a stream", "[serial]") {
    const auto clip = parse_voxa(
        "# comment\n"
        "clip walk\n"
        "track body 60\n"
        "  channel position\n"
        "    k 0 0 0 0 linear 0 1\n"
        "    k 1.5 0 1 0 linear 0 1\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE(*clip != nullptr);
    REQUIRE((*clip)->get_name() == "walk");
    REQUIRE((*clip)->has_track("body"));
    REQUIRE((*clip)->get_tracks().size() == 1);
}

TEST_CASE("a truncated voxa keyframe is a parse error", "[serial]") {
    const auto clip = parse_voxa(
        "clip walk\n"
        "track body 60\n"
        "  channel position\n"
        "    k 0 0\n"
    );

    REQUIRE_FALSE(clip.has_value());
    REQUIRE(clip.error() == asset::voxa_deserializer::error_type::parse_error);
}

TEST_CASE("the parsers survive rubbish", "[serial]") {
    const std::array<std::string_view, 6> rubbish{
        "\0\0\0", "clip", "track", "k k k", "v v v", "root\n\n\nentity\n",
    };

    for (const auto& text : rubbish) {
        static_cast<void>(parse_vox(text));
        static_cast<void>(parse_voxa(text));
    }

    SUCCEED("neither parser crashed");
}

TEST_CASE("a vox file of an unsupported major version is rejected", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 99.0\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::unsupported_version);
}

TEST_CASE("a vox 3.1 file is rejected by version", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 3.1\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::unsupported_version);
}

TEST_CASE("a vox file of the current version parses", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 4.0\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

TEST_CASE("a vox file of a newer minor version parses", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 4.7\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
}

TEST_CASE("a vox file without a version header parses", "[serial]") {
    const auto prefab = parse_vox(
        "# just a comment\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

TEST_CASE("a voxa file of an unsupported major version is rejected", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 99.0\n"
        "clip walk 60\n"
    );

    REQUIRE_FALSE(clip.has_value());
    REQUIRE(clip.error() == asset::voxa_deserializer::error_type::unsupported_version);
}

TEST_CASE("a voxa file of the current version parses", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 1.1\n"
        "clip walk 60\n"
        "rig humanoid\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE((*clip)->get_rig() == "humanoid");
}

TEST_CASE("a voxa file without a rig parses as one without a rig", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 1.0\n"
        "clip walk 60\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE((*clip)->get_rig().empty());
}

namespace {

auto parse_voxm(asset::model_registry& registry, std::string_view text) {
    static const voxel_registry voxel_types;

    std::istringstream input{std::string{text}};
    asset::voxm_deserializer deserializer{registry, voxel_types};
    return deserializer.deserialize(input);
}

auto write_voxm(const asset::model& model) -> std::string {
    std::ostringstream output;
    asset::voxm_serializer{model}.serialize(output);
    return output.str();
}

}  // namespace

TEST_CASE("a voxm volume survives a round trip", "[serial]") {
    asset::model_registry registry;

    const auto source = registry.create_unnamed(voxels::palette::category, vec3i{6, 4, 3});
    source->set_pivot(vec3f{2.5F, 1.5F, 0.5F});
    source->set_voxel(vec3i{0, 0, 0}, voxels::palette::gray[7]);
    source->set_voxel(vec3i{1, 0, 0}, voxels::palette::gray[7]);
    source->set_voxel(vec3i{2, 0, 0}, voxels::palette::gray[8]);
    source->set_voxel(vec3i{5, 3, 2}, voxels::palette::gray[9]);

    const auto restored = parse_voxm(registry, write_voxm(*source));

    REQUIRE(restored.has_value());

    const auto& model = **restored;
    REQUIRE(model.size() == source->size());
    REQUIRE(model.category() == source->category());
    REQUIRE(model.pivot() == source->pivot());

    for (int32 z = 0; z < model.depth(); ++z) {
        for (int32 y = 0; y < model.height(); ++y) {
            for (int32 x = 0; x < model.width(); ++x) {
                REQUIRE(model.get_voxel(x, y, z) == source->get_voxel(x, y, z));
            }
        }
    }
}

TEST_CASE("a voxm run collapses a row of equal voxels", "[serial]") {
    asset::model_registry registry;

    const auto source = registry.create_unnamed(voxels::world::category, vec3i{8, 1, 1});
    for (int32 x = 0; x < 8; ++x) {
        source->set_voxel(vec3i{x, 0, 0}, voxels::world::grass[0]);
    }

    const auto text = write_voxm(*source);

    std::istringstream lines{text};
    std::string line;
    int32 runs = 0;
    while (std::getline(lines, line)) {
        if (line.starts_with("r ")) {
            ++runs;
        }
    }

    REQUIRE(runs == 1);
    REQUIRE(text.contains("r 0 0 0 8 "));
}

TEST_CASE("an empty voxm volume is a valid file", "[serial]") {
    asset::model_registry registry;

    const auto source   = registry.create_unnamed(voxels::palette::category, vec3i{4, 4, 4});
    const auto restored = parse_voxm(registry, write_voxm(*source));

    REQUIRE(restored.has_value());
    REQUIRE((*restored)->size() == vec3i{4, 4, 4});
}

TEST_CASE("a voxm file of an unsupported major version is rejected", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "# Voxm File Version 99.0\n"
        "category 2\n"
        "size 2 2 2\n"
    );

    REQUIRE_FALSE(restored.has_value());
    REQUIRE(restored.error() == asset::voxm_deserializer::error_type::unsupported_version);
}

TEST_CASE("a voxm run outside the volume is a parse error", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "category 2\n"
        "size 2 2 2\n"
        "r 0 0 0 5 51\n"
    );

    REQUIRE_FALSE(restored.has_value());
}

TEST_CASE("a voxm file without a size is a parse error", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(registry, "category 2\n");

    REQUIRE_FALSE(restored.has_value());
}

TEST_CASE("a voxm voxel outside the catalog still parses", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "category 200\n"
        "size 2 2 2\n"
        "r 0 0 0 1 7\n"
    );

    REQUIRE(restored.has_value());
    REQUIRE((*restored)->get_voxel(0, 0, 0) == voxel{voxel_category{200}, 7});
}
