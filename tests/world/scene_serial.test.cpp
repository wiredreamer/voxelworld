#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;
using Catch::Approx;

namespace {

constexpr std::string_view identity_transform = "0 0 0\t0 0 0\t1 1 1";

auto make_node(std::string name, std::string parent) -> asset::vox_entity_data {
    asset::vox_entity_data data;
    data.name        = std::move(name);
    data.parent_name = std::move(parent);
    return data;
}

auto transform_value(const vec3f& position, const vec3f& rotation) -> std::string {
    return std::format(
        "{} {} {}\t{} {} {}\t1 1 1", position.x, position.y, position.z, rotation.x, rotation.y,
        rotation.z
    );
}

struct scene_fixture final {
    world w;
    voxel_registry voxel_types;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), voxel_types, "."};

    [[nodiscard]] auto deserializer() -> vox_deserializer {
        return vox_deserializer{w, parser, library};
    }
};

}  // namespace

TEST_CASE("a child declared before its parent still gets attached", "[scene]") {
    scene_fixture fx;
    auto& w           = fx.w;
    auto deserializer = fx.deserializer();

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
    scene_fixture fx;
    auto& w           = fx.w;
    auto deserializer = fx.deserializer();

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));
    prefab.entities.push_back(make_node("child", "root"));

    const auto res = deserializer.instantiate(prefab, {});

    const auto child  = res.name_to_entity.at("child");
    const auto parent = res.name_to_entity.at("root");

    REQUIRE(w.get<hierarchy_component>(child).get_parent() == parent);
}

TEST_CASE("a node with a missing parent survives", "[scene]") {
    scene_fixture fx;
    auto& w           = fx.w;
    auto deserializer = fx.deserializer();

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));
    prefab.entities.push_back(make_node("orphan", "nowhere"));

    const auto res = deserializer.instantiate(prefab, {});

    REQUIRE(res.entities.size() == 2);
    REQUIRE(res.name_to_entity.contains("orphan"));
}

TEST_CASE("the pivot of a parent does not move its child", "[scene]") {
    scene_fixture fx;
    auto& w      = fx.w;
    auto& models = w.resource<asset::model_registry>();

    const asset::asset_ref root_ref{"models/m_human/root.voxm"};
    const asset::asset_ref hand_ref{"models/m_human/hand_right.voxm"};

    auto root_volume = models.create_unnamed(vec3i{12, 12, 12});
    root_volume->set_pivot(vec3f{6.0F, 6.0F, 6.0F});
    fx.library.adopt(root_ref, root_volume);

    auto hand_volume = models.create_unnamed(vec3i{5, 7, 5});
    hand_volume->set_pivot(vec3f{2.5F, 3.5F, 2.5F});
    fx.library.adopt(hand_ref, hand_volume);

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";

    asset::vox_entity_data root;
    root.name = "root";
    root.add("transform", std::string{identity_transform});
    root.add("model", root_ref.str());
    prefab.entities.push_back(root);

    constexpr auto hand_position = vec3f{10.0F, 2.0F, 0.0F};
    constexpr auto hand_rotation = vec3f{1.5707963F, 0.0F, 0.0F};

    asset::vox_entity_data hand;
    hand.name        = "hand_right";
    hand.parent_name = "root";
    hand.add("transform", transform_value(hand_position, hand_rotation));
    hand.add("model", hand_ref.str());
    prefab.entities.push_back(hand);

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(prefab, {});
    w.update(0.016F);

    const auto ent = res.name_to_entity.at("hand_right");

    const auto volume_matrix =
        model_matrix(w.get<transform_component>(ent), w.get<model_component>(ent));

    const auto expected =
        math::transform_matrix(hand_position, hand_rotation, vec3f{1.0F, 1.0F, 1.0F}) *
        math::translation_matrix(-hand_volume->pivot());

    REQUIRE(math::approx_equal(volume_matrix, expected));
}

TEST_CASE("a rig name survives a round trip through the world", "[scene]") {
    scene_fixture fx;
    auto& w = fx.w;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.rig       = "humanoid";

    asset::vox_entity_data root;
    root.name = "root";
    root.add("transform", std::string{identity_transform});
    prefab.entities.push_back(root);

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(prefab, {});
    w.update(0.016F);

    const auto root_ent = res.name_to_entity.at("root");
    REQUIRE(w.has<rig_component>(root_ent));
    REQUIRE(w.get<rig_component>(root_ent).get_name() == "humanoid");

    asset::vox_writer_plain writer;
    vox_serializer serializer{w, writer, root_ent, {.entity_names = res.entity_to_name}};

    REQUIRE(serializer.extract().rig == "humanoid");
}

TEST_CASE("a prefab keeps the machines it names through a round trip", "[scene]") {
    scene_fixture fx;
    auto& w = fx.w;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.fsm_refs  = {
        asset::asset_ref{"fsm/humanoid_locomotion.voxf"},
        asset::asset_ref{"fsm/humanoid_action.voxf"},
    };

    asset::vox_entity_data root;
    root.name = "root";
    root.add("transform", std::string{identity_transform});
    prefab.entities.push_back(root);

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(prefab, {});
    w.update(0.016F);

    const auto root_ent = res.name_to_entity.at("root");
    REQUIRE(w.has<animation_machines_component>(root_ent));

    const auto sources = w.get<animation_machines_component>(root_ent).get_sources();
    REQUIRE(sources.size() == 2);
    REQUIRE(sources[0] == asset::asset_ref{"fsm/humanoid_locomotion.voxf"});

    asset::vox_writer_plain writer;
    vox_serializer serializer{w, writer, root_ent, {.entity_names = res.entity_to_name}};

    REQUIRE(serializer.extract().fsm_refs == prefab.fsm_refs);
}

TEST_CASE("a prefab without a rig gets no rig component", "[scene]") {
    scene_fixture fx;
    auto& w = fx.w;

    asset::vox_prefab_data prefab;
    prefab.root_name = "root";
    prefab.entities.push_back(make_node("root", ""));

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(prefab, {});

    REQUIRE_FALSE(w.has<rig_component>(res.name_to_entity.at("root")));
}

TEST_CASE("a model ref survives a round trip through the world", "[scene]") {
    scene_fixture fx;
    auto& w      = fx.w;
    auto& models = w.resource<asset::model_registry>();

    const asset::asset_ref ref{"models/m_human/body.voxm"};
    fx.library.adopt(ref, models.create_unnamed(vec3i{4, 4, 4}));

    asset::vox_prefab_data prefab;
    prefab.root_name = "body";

    constexpr auto body_position = vec3f{1.0F, 2.0F, 3.0F};

    asset::vox_entity_data body;
    body.name = "body";
    body.add("transform", transform_value(body_position, vec3f{}));
    body.add("model", ref.str());
    prefab.entities.push_back(body);

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(prefab, {});
    w.update(0.016F);

    asset::vox_writer_plain writer;
    vox_serializer serializer{
        w, writer, res.name_to_entity.at("body"), {.entity_names = res.entity_to_name}
    };

    const auto written = serializer.extract();

    REQUIRE(written.entities.size() == 1);
    REQUIRE(written.entities.front().value_of("model") == ref.str());

    std::array<float32, 9> values{};
    REQUIRE(asset::parse_floats(written.entities.front().value_of("transform"), values));
    REQUIRE(vec3f{values[0], values[1], values[2]} == body_position);
}

TEST_CASE("a node with a missing model still loads", "[scene]") {
    scene_fixture fx;
    auto& w           = fx.w;
    auto deserializer = fx.deserializer();

    asset::vox_prefab_data prefab;
    prefab.root_name = "body";

    asset::vox_entity_data body;
    body.name = "body";
    body.add("transform", std::string{identity_transform});
    body.add("model", "models/nothing.voxm");
    prefab.entities.push_back(body);

    const auto res = deserializer.instantiate(prefab, {});
    w.update(0.016F);

    const auto ent = res.name_to_entity.at("body");
    REQUIRE(res.entities.size() == 1);
    REQUIRE_FALSE(w.has<model_component>(ent));
}

TEST_CASE("moving the pivot reports a transform change", "[scene]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    auto model = models.create_unnamed(vec3i{4, 4, 4});
    model->fill(voxels::green[4]);

    const auto ent = w.create()
        .with<transform_component>()
        .with<model_component>()
        .with<spatial_component>()
        .get_entity();

    w.system<model_system>().modify(ent).set_model(model);
    w.update(0.016F);
    w.clear_changed();

    w.system<model_system>().modify(ent).set_pivot(vec3f{2.0F, 0.0F, 0.0F});
    w.update(0.016F);

    REQUIRE(std::ranges::contains(w.changed<transform_component>(), ent));
    REQUIRE(w.get<model_component>(ent).get_pivot() == vec3f{2.0F, 0.0F, 0.0F});
}
