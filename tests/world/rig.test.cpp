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

auto make_target(std::string name, std::string parent) -> asset::vox_entity_data {
    asset::vox_entity_data data;
    data.name        = name;
    data.parent_name = std::move(parent);
    data.add("transform", std::string{identity_transform});
    data.add("anim_target", std::move(name));
    return data;
}

// Дерево из assets/prefabs/m_human.vox в миниатюре: корень без цели и два узла
// с целями. Риг проверяется против настоящего дерева, поэтому и в тесте цели
// берутся из узлов, а не из списка рядом.
struct rig_fixture final {
    world w;
    voxel_registry voxel_types;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), voxel_types, "."};

    vox_deserializer::result res;

    auto load(std::string_view rig) -> void {
        asset::vox_prefab_data prefab;
        prefab.root_name = "root";
        prefab.rig       = rig;

        asset::vox_entity_data root;
        root.name = "root";
        root.add("transform", std::string{identity_transform});
        prefab.entities.push_back(root);
        prefab.entities.push_back(make_target("body", "root"));
        prefab.entities.push_back(make_target("head", "root"));

        vox_deserializer deserializer{w, parser, library};
        res = deserializer.instantiate(prefab, {});
        w.update(0.016F);
    }

    [[nodiscard]] auto root() const -> entity {
        return res.name_to_entity.at("root");
    }

    [[nodiscard]] auto anim() -> animation_system& {
        return w.system<animation_system>();
    }
};

auto make_clip(std::string_view rig, std::initializer_list<std::string_view> targets)
    -> asset::animation_clip {
    asset::animation_clip clip{"a_walk"};
    clip.set_rig(std::string{rig});
    for (const auto target : targets) {
        clip.add_track(asset::animation_track{std::string{target}});
    }
    return clip;
}

}  // namespace

// Список целей не лежит ни в файле, ни рядом — он и есть дерево. Дублировать
// его значит завести второй источник правды, который разойдётся с первым.
TEST_CASE("the targets of a rig come from the nodes that carry them", "[rig]") {
    rig_fixture fx;
    fx.load("humanoid");

    const auto targets = fx.anim().collect_targets(fx.root());

    REQUIRE(targets == std::vector<std::string>{"body", "head"});
}

TEST_CASE("a clip of the same rig passes the check", "[rig]") {
    rig_fixture fx;
    fx.load("humanoid");

    const auto clip   = make_clip("humanoid", {"body", "head"});
    const auto report = fx.anim().check_clip(fx.root(), clip);

    REQUIRE(report.ok());
    REQUIRE(report.rig == "humanoid");
    REQUIRE(report.clip_rig == "humanoid");
    REQUIRE(report.unknown_targets.empty());
}

// Сообщение обязано называть оба имени: «клип не подошёл» не говорит ничего,
// а «клип для goblin, документ humanoid» говорит всё.
TEST_CASE("a clip of a foreign rig names both rigs", "[rig]") {
    rig_fixture fx;
    fx.load("humanoid");

    const auto clip   = make_clip("goblin", {"body", "head"});
    const auto report = fx.anim().check_clip(fx.root(), clip);

    REQUIRE_FALSE(report.ok());
    REQUIRE_FALSE(report.rig_matches());
    REQUIRE(report.rig == "humanoid");
    REQUIRE(report.clip_rig == "goblin");
}

// Совпавшего имени рига мало: имя — дешёвая проверка, а настоящая идёт по
// целям. Дорожка без узла не проиграется, и молчать об этом нельзя.
TEST_CASE("a track without a node is reported even when the rigs match", "[rig]") {
    rig_fixture fx;
    fx.load("humanoid");

    const auto clip   = make_clip("humanoid", {"body", "tail"});
    const auto report = fx.anim().check_clip(fx.root(), clip);

    REQUIRE(report.rig_matches());
    REQUIRE_FALSE(report.ok());
    REQUIRE(report.unknown_targets == std::vector<std::string>{"tail"});
}

// Клип старше проверки рига не виноват в том, что её тогда не было: пустое имя
// с любой стороны — «не указан», а не «не совпало».
TEST_CASE("a clip without a rig is not a mismatch", "[rig]") {
    rig_fixture fx;
    fx.load("humanoid");

    const auto clip   = make_clip("", {"body", "head"});
    const auto report = fx.anim().check_clip(fx.root(), clip);

    REQUIRE(report.ok());
    REQUIRE(report.clip_rig.empty());
}

TEST_CASE("a document without a rig checks nothing but the targets", "[rig]") {
    rig_fixture fx;
    fx.load("");

    REQUIRE_FALSE(fx.w.has<rig_component>(fx.root()));

    const auto report = fx.anim().check_clip(fx.root(), make_clip("goblin", {"body"}));

    REQUIRE(report.ok());
    REQUIRE(report.rig.empty());
}
