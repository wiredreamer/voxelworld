#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

struct probe_component final {
    float32 range = 0.0F;
    std::string colour;
};

auto make_probe_codec() -> component_codec {
    component_codec codec;
    codec.tag = "probe";

    codec.read = [](const component_read& in) {
        in.target.modify(in.ent).with<probe_component>();

        std::array<float32, 1> range{};
        static_cast<void>(asset::parse_floats(in.tags.front()->prop("range"), range));

        auto& probe  = in.target.get<probe_component>(in.ent);
        probe.range  = range[0];
        probe.colour = std::string{in.tags.front()->prop("colour")};
    };

    codec.write = [](const component_write& out) {
        const auto& probe = out.source.get<probe_component>(out.ent);
        out.out.add("probe")
            .set_prop("range", std::format("{}", probe.range))
            .set_prop("colour", probe.colour);
    };

    return codec;
}

struct codec_fixture final {
    world w;
    voxel_registry voxel_types;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), voxel_types, "."};
    component_registry codecs;

    [[nodiscard]] auto instantiate(const asset::vox_prefab_data& prefab)
        -> vox_deserializer::result {
        vox_deserializer deserializer{w, parser, library, codecs};
        auto res = deserializer.instantiate(prefab, {});
        w.update(0.016F);
        return res;
    }
};

}  // namespace

TEST_CASE("a component the engine knows nothing about survives a round trip", "[codec]") {
    codec_fixture fx;
    fx.codecs.register_for<probe_component>(make_probe_codec());

    asset::vox_prefab_data prefab;
    prefab.root_name = "probe_node";

    asset::vox_entity_data node;
    node.name = "probe_node";
    node.add("transform", "1 2 3\t0 0 0\t1 1 1");
    node.add("probe").set_prop("range", "12.5").set_prop("colour", "amber");
    prefab.entities.push_back(node);

    const auto res = fx.instantiate(prefab);
    const auto ent = res.name_to_entity.at("probe_node");

    REQUIRE(fx.w.has<probe_component>(ent));
    REQUIRE(fx.w.get<probe_component>(ent).range == 12.5F);
    REQUIRE(fx.w.get<probe_component>(ent).colour == "amber");

    asset::vox_writer_plain writer;
    vox_serializer serializer{
        fx.w, writer, ent, {.entity_names = res.entity_to_name}, fx.codecs
    };

    const auto written = serializer.extract();
    REQUIRE(written.entities.size() == 1);

    const auto* probe = written.entities.front().find("probe");
    REQUIRE(probe != nullptr);
    REQUIRE(probe->prop("range") == "12.5");
    REQUIRE(probe->prop("colour") == "amber");
}

TEST_CASE("a tag without a codec is ignored, not lost", "[codec]") {
    codec_fixture fx;

    asset::vox_prefab_data prefab;
    prefab.root_name = "node";

    asset::vox_entity_data node;
    node.name = "node";
    node.add("transform", "0 0 0\t0 0 0\t1 1 1");
    node.add("probe").set_prop("range", "12.5");
    prefab.entities.push_back(node);

    const auto res = fx.instantiate(prefab);
    REQUIRE(fx.w.has<transform_component>(res.name_to_entity.at("node")));
    REQUIRE_FALSE(fx.w.has<probe_component>(res.name_to_entity.at("node")));

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, prefab).has_value());

    asset::vox_parser_plain parser;
    std::istringstream input{written.str()};
    const auto reread = parser.parse(input);

    REQUIRE(reread.has_value());
    REQUIRE(reread->entities.front().find("probe")->prop("range") == "12.5");
}

TEST_CASE("the transform phase runs before the general one", "[codec]") {
    codec_fixture fx;

    asset::vox_prefab_data prefab;
    prefab.root_name = "body";

    asset::vox_entity_data node;
    node.name = "body";

    node.add("anim_target", "body");
    node.add("transform", "4 5 6\t0 0 0\t1 1 1");
    prefab.entities.push_back(node);

    const auto res = fx.instantiate(prefab);
    const auto ent = res.name_to_entity.at("body");

    REQUIRE(fx.w.has<animation_target_component>(ent));
    REQUIRE(
        fx.w.get<animation_target_component>(ent).get_rest_transform().get_position() ==
        vec3f{4.0F, 5.0F, 6.0F}
    );
}
