#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

auto make_node(std::string name, std::string parent) -> asset::vox_entity_data {
    asset::vox_entity_data data;
    data.name        = std::move(name);
    data.parent_name = std::move(parent);
    return data;
}

// Разборщик тут не нужен: проверяется применение префаба к реестру, а не чтение
// файла. Ссылка обязана быть живой всё время жизни десериализатора.
auto make_deserializer(world& w, asset::vox_parser_plain& parser) -> vox_deserializer {
    return vox_deserializer{w, parser};
}

}  // namespace

// Порядок узлов в файле — дело писателя, а не читателя: обход дерева при записи
// может выдать ребёнка раньше родителя, и связь от этого теряться не должна.
TEST_CASE("a child declared before its parent still gets attached", "[scene]") {
    world w;
    const block_registry blocks;
    asset::vox_parser_plain parser{blocks};
    auto deserializer = make_deserializer(w, parser);

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("child", "root"));
    prefab.entities.push_back(make_node("root", ""));

    const auto res = deserializer.instantiate(prefab, {});

    REQUIRE(res.entities.size() == 2);
    REQUIRE(res.name_to_entity.contains("child"));
    REQUIRE(res.name_to_entity.contains("root"));

    const auto child  = res.name_to_entity.at("child");
    const auto parent = res.name_to_entity.at("root");

    REQUIRE(w.get<hierarchy_component>(child).get_parent() == parent);
}

TEST_CASE("a parent declared before its child still gets attached", "[scene]") {
    world w;
    const block_registry blocks;
    asset::vox_parser_plain parser{blocks};
    auto deserializer = make_deserializer(w, parser);

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));
    prefab.entities.push_back(make_node("child", "root"));

    const auto res = deserializer.instantiate(prefab, {});

    const auto child  = res.name_to_entity.at("child");
    const auto parent = res.name_to_entity.at("root");

    REQUIRE(w.get<hierarchy_component>(child).get_parent() == parent);
}

// Битая ссылка на родителя не повод терять узел: он встанет в корень, о чём
// сказано в логе, и документ откроется целиком.
TEST_CASE("a node with a missing parent survives", "[scene]") {
    world w;
    const block_registry blocks;
    asset::vox_parser_plain parser{blocks};
    auto deserializer = make_deserializer(w, parser);

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));
    prefab.entities.push_back(make_node("orphan", "nowhere"));

    const auto res = deserializer.instantiate(prefab, {});

    REQUIRE(res.entities.size() == 2);
    REQUIRE(res.name_to_entity.contains("orphan"));
}
