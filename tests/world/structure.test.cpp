#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr std::string_view identity_transform = "0 0 0\t0 0 0\t1 1 1";

struct structure_fixture final {
    world w;
    voxel_registry voxel_types;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), voxel_types, "."};

    [[nodiscard]] auto instantiate(const asset::vox_prefab_data& prefab)
        -> vox_deserializer::result {
        vox_deserializer deserializer{w, parser, library};
        auto res = deserializer.instantiate(prefab, {});
        w.update(0.016F);
        return res;
    }

    [[nodiscard]] auto extract(const vox_deserializer::result& res) -> asset::vox_prefab_data {
        asset::vox_writer_plain writer;
        vox_serializer serializer{
            w, writer, res.name_to_entity.at(res.root_name),
            {.entity_names = res.entity_to_name}
        };
        return serializer.extract();
    }
};

auto make_node(std::string name, std::string parent) -> asset::vox_entity_data {
    asset::vox_entity_data data;
    data.name        = std::move(name);
    data.parent_name = std::move(parent);
    data.add("transform", std::string{identity_transform});
    return data;
}

}  // namespace

TEST_CASE("structure metadata survives a round trip", "[structure]") {
    structure_fixture fx;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";

    auto root = make_node("root", "");
    root.add("structure")
        .set_prop("type", "tavern")
        .set_prop("race", "human elf")
        .set_prop("tier", "3")
        .set_prop("size", "M");
    prefab.entities.push_back(std::move(root));

    const auto res      = fx.instantiate(prefab);
    const auto root_ent = res.name_to_entity.at("root");

    REQUIRE(fx.w.has<structure_component>(root_ent));

    const auto& structure = fx.w.get<structure_component>(root_ent);
    REQUIRE(structure.get_type() == "tavern");
    REQUIRE(structure.get_races().size() == 2);
    REQUIRE(structure.get_races()[1] == "elf");
    REQUIRE(structure.get_tier() == 3);
    REQUIRE(structure.get_size() == structure_size::medium);

    const auto written = fx.extract(res);
    const auto* tag    = written.entities.front().find("structure");

    REQUIRE(tag != nullptr);
    REQUIRE(tag->prop("type") == "tavern");
    REQUIRE(tag->prop("race") == "human elf");
    REQUIRE(tag->prop("tier") == "3");
    REQUIRE(tag->prop("size") == "M");
}

TEST_CASE("an unset structure field is not written", "[structure]") {
    structure_fixture fx;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";

    auto root = make_node("root", "");
    root.add("structure").set_prop("type", "corridor");
    prefab.entities.push_back(std::move(root));

    const auto res     = fx.instantiate(prefab);
    const auto written = fx.extract(res);
    const auto* tag    = written.entities.front().find("structure");

    REQUIRE(tag != nullptr);
    REQUIRE(tag->prop("type") == "corridor");
    REQUIRE(tag->prop("tier").empty());
    REQUIRE(tag->prop("size").empty());
    REQUIRE(tag->prop("race").empty());
}

TEST_CASE("furniture and connection points are ordinary nodes", "[structure]") {
    structure_fixture fx;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));

    auto table = make_node("table_spot", "root");
    table.add("furniture", "table");
    prefab.entities.push_back(std::move(table));

    auto door = make_node("door_north", "root");
    door.add("connection", "door");
    prefab.entities.push_back(std::move(door));

    const auto res = fx.instantiate(prefab);

    const auto table_ent = res.name_to_entity.at("table_spot");
    const auto door_ent  = res.name_to_entity.at("door_north");

    REQUIRE(fx.w.get<furniture_point_component>(table_ent).get_category() == "table");
    REQUIRE(fx.w.get<connection_point_component>(door_ent).get_profile() == "door");

    REQUIRE_FALSE(fx.w.has<connection_point_component>(table_ent));
    REQUIRE_FALSE(fx.w.has<furniture_point_component>(door_ent));

    const auto written = fx.extract(res);
    REQUIRE(written.entities.size() == 3);

    const auto* table_tag = std::ranges::find(
                                written.entities, "table_spot", &asset::vox_entity_data::name
                            )->find("furniture");
    REQUIRE(table_tag != nullptr);
    REQUIRE(table_tag->value == "table");
}
