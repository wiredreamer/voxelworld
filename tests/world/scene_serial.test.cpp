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

// Разборщик и библиотека живут дольше десериализатора, поэтому собраны в одно
// место: проверяется применение префаба к реестру, а не чтение файла.
struct scene_fixture final {
    world w;
    block_registry blocks;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), blocks, "."};

    [[nodiscard]] auto deserializer() -> vox_deserializer {
        return vox_deserializer{w, parser, library};
    }
};

}  // namespace

// Порядок узлов в файле — дело писателя, а не читателя: обход дерева при записи
// может выдать ребёнка раньше родителя, и связь от этого теряться не должна.
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

// Битая ссылка на родителя не повод терять узел: он встанет в корень, о чём
// сказано в логе, и документ откроется целиком.
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

// Точка вращения принадлежит объёму, а не узлу: она входит в матрицу самого
// объёма и детям не спускается. Числа взяты из assets/prefabs/m_human.vox — до
// переезда origin стоял в узле, и родительский вычитался при спуске к ребёнку,
// так что ошибка здесь сдвинула бы руку ровно на точку корня.
TEST_CASE("the pivot of a parent does not move its child", "[scene]") {
    scene_fixture fx;
    auto& w      = fx.w;
    auto& models = w.resource<asset::model_registry>();

    const asset::asset_ref root_ref{"models/m_human/root.voxm"};
    const asset::asset_ref hand_ref{"models/m_human/hand_right.voxm"};

    auto root_volume = models.create_unnamed(blocks::terrain::category, vec3i{12, 12, 12});
    root_volume->set_pivot(vec3f{6.0F, 6.0F, 6.0F});
    fx.library.adopt(root_ref, root_volume);

    auto hand_volume = models.create_unnamed(blocks::terrain::category, vec3i{5, 7, 5});
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

    // Матрица, которой рисуется объём: узел плюс собственная точка модели.
    const auto volume_matrix =
        model_matrix(w.get<transform_component>(ent), w.get<model_component>(ent));

    const auto expected =
        math::transform_matrix(hand_position, hand_rotation, vec3f{1.0F, 1.0F, 1.0F}) *
        math::translation_matrix(-hand_volume->pivot());

    REQUIRE(math::approx_equal(volume_matrix, expected));
}

// Узел 3.0 не носит вокселей: он называет .voxm, и это имя обязано вернуться из
// записи тем же. Писатель и разборщик лежат в разных модулях и расходятся молча.
// Риг едет в мир и обратно через компонент на корне: в файле он одна строка в
// шапке, а в мире — свойство корневой сущности, и потеряться между ними нельзя.
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

// Риг необязателен: у меча и стрелы его нет, и заводить компонент ради пустой
// строки незачем — иначе в шапку поедет «rig » без имени.
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
    fx.library.adopt(ref, models.create_unnamed(blocks::terrain::category, vec3i{4, 4, 4}));

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

    // Сверяются числа, а не их написание: поворот едет в мир кватернионом и
    // возвращается эйлерами, у которых ноль бывает со знаком.
    std::array<float32, 9> values{};
    REQUIRE(asset::parse_floats(written.entities.front().value_of("transform"), values));
    REQUIRE(vec3f{values[0], values[1], values[2]} == body_position);
}

// Битая ссылка не повод ронять загрузку: узел встаёт без модели, о чём сказано
// в логе, и остальной префаб открывается целиком.
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

// Правка точки вращения двигает объём, а не перестраивает его, и заявлена она
// обязана быть по трансформу: матрицу в буфере рендера переписывает именно эта
// ветка. По границам это не проверить — они пересчитываются и от модели тоже,
// поэтому мимо цели прошла бы любая из двух заявок.
TEST_CASE("moving the pivot reports a transform change", "[scene]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    auto model = models.create_unnamed(blocks::terrain::category, vec3i{4, 4, 4});
    model->fill(voxel{blocks::terrain::grass[0]});

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

    REQUIRE(w.changed<transform_component>().contains(ent));
    REQUIRE(w.get<model_component>(ent).get_pivot() == vec3f{2.0F, 0.0F, 0.0F});
}
