#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

const asset::asset_ref small_ref{"models/m_human/head.voxm"};
const asset::asset_ref big_ref{"models/m_human/head_big.voxm"};

struct variant_fixture final {
    world w;
    block_registry blocks;
    asset::vox_parser_plain parser;
    asset::model_library library{w.resource<asset::model_registry>(), blocks, "."};

    variant_fixture() {
        auto& models = w.resource<asset::model_registry>();

        // Две головы разного размера и с разными точками вращения: подмена
        // обязана быть видна, а узел — остаться на месте.
        auto small = models.create_unnamed(blocks::palette::category, vec3i{4, 4, 4});
        small->set_pivot(vec3f{2.0F, 2.0F, 2.0F});
        library.adopt(small_ref, small);

        auto big = models.create_unnamed(blocks::palette::category, vec3i{8, 10, 8});
        big->set_pivot(vec3f{4.0F, 5.0F, 4.0F});
        library.adopt(big_ref, big);
    }

    [[nodiscard]] auto instantiate(const asset::vox_prefab_data& prefab)
        -> vox_deserializer::result {
        vox_deserializer deserializer{w, parser, library};
        auto res = deserializer.instantiate(prefab, {});
        w.update(0.016F);
        return res;
    }
};

auto make_prefab() -> asset::vox_prefab_data {
    asset::vox_prefab_data prefab;
    prefab.root_name = "head";

    asset::vox_entity_data head;
    head.name = "head";
    head.add("transform", "0 11 0\t0 0 0\t1 1 1");
    head.add("model", small_ref.str());
    head.add("variant", "head")
        .set_prop("candidate", small_ref.str())
        .set_prop("candidate", big_ref.str())
        .set_prop("selected", "0")
        .set_prop("target", "head")
        .set_prop("socket", "helmet");
    prefab.entities.push_back(head);

    asset::vox_entity_data helmet;
    helmet.name        = "helmet";
    helmet.parent_name = "head";
    helmet.add("transform", "0 3 0\t0 0 0\t1 1 1");
    prefab.entities.push_back(helmet);

    return prefab;
}

}  // namespace

TEST_CASE("a variant slot survives a round trip through the world", "[variant]") {
    variant_fixture fx;

    const auto res  = fx.instantiate(make_prefab());
    const auto head = res.name_to_entity.at("head");

    REQUIRE(fx.w.has<variant_slot_component>(head));

    const auto& slot = fx.w.get<variant_slot_component>(head);
    REQUIRE(slot.get_name() == "head");
    REQUIRE(slot.get_candidates().size() == 2);
    REQUIRE(slot.get_candidates().back() == big_ref);
    REQUIRE(slot.get_selected() == 0);
    REQUIRE(slot.required_targets() == std::vector<std::string>{"head"});
    REQUIRE(slot.required_sockets() == std::vector<std::string>{"helmet"});

    asset::vox_writer_plain writer;
    vox_serializer serializer{fx.w, writer, head, {.entity_names = res.entity_to_name}};

    const auto written = serializer.extract();
    const auto* tag    = written.entities.front().find("variant");

    REQUIRE(tag != nullptr);
    REQUIRE(tag->value == "head");
    REQUIRE(tag->prop("candidate") == small_ref.str());
    REQUIRE(tag->prop("selected") == "0");
    REQUIRE(tag->prop("target") == "head");
}

// Главная проверка слота: голова меняется, а шея и шлем на сокете стоят. Держит
// это точка вращения, которая живёт в самом объёме, — узел её не знает и не
// двигается.
TEST_CASE("switching a variant leaves the node and its subtree in place", "[variant]") {
    variant_fixture fx;

    const auto res    = fx.instantiate(make_prefab());
    const auto head   = res.name_to_entity.at("head");
    const auto helmet = res.name_to_entity.at("helmet");

    const auto head_before   = fx.w.get<transform_component>(head).get_world_matrix();
    const auto helmet_before = fx.w.get<transform_component>(helmet).get_world_matrix();

    REQUIRE(fx.w.system<variant_system>().apply(head, fx.library, 1).has_value());
    fx.w.update(0.016F);

    REQUIRE(fx.w.get<model_component>(head).get_source() == big_ref);
    REQUIRE(fx.w.get<model_component>(head).size() == vec3i{8, 10, 8});
    REQUIRE(fx.w.get<variant_slot_component>(head).get_selected() == 1);

    REQUIRE(math::approx_equal(
        fx.w.get<transform_component>(head).get_world_matrix(), head_before
    ));
    REQUIRE(math::approx_equal(
        fx.w.get<transform_component>(helmet).get_world_matrix(), helmet_before
    ));
}

// Номер выбранного — это позиция, и список под ним двигается: выбор обязан
// держаться за ссылку, иначе удаление соседа сверху молча переводит слот на
// другого кандидата.
TEST_CASE("the selection follows its candidate, not its number", "[variant]") {
    variant_fixture fx;

    const auto res  = fx.instantiate(make_prefab());
    const auto head = res.name_to_entity.at("head");

    auto& variants = fx.w.system<variant_system>();
    REQUIRE(variants.apply(head, fx.library, 1).has_value());

    // Убираем кандидата, стоявшего выше выбранного.
    const bool moved = variants.modify(head).set_candidates({big_ref});

    REQUIRE_FALSE(moved);
    REQUIRE(fx.w.get<variant_slot_component>(head).get_selected() == 0);
    REQUIRE(fx.w.get<variant_slot_component>(head).selected_ref() == big_ref);
}

// А вот если убрали самого выбранного, слот обязан сказать об этом: в сцене
// остался кандидат, которого в списке больше нет.
TEST_CASE("dropping the chosen candidate is reported", "[variant]") {
    variant_fixture fx;

    const auto res  = fx.instantiate(make_prefab());
    const auto head = res.name_to_entity.at("head");

    auto& variants = fx.w.system<variant_system>();
    REQUIRE(variants.apply(head, fx.library, 1).has_value());

    const bool moved = variants.modify(head).set_candidates({small_ref});

    REQUIRE(moved);
    REQUIRE(fx.w.get<variant_slot_component>(head).get_selected() == 0);
    REQUIRE(fx.w.get<variant_slot_component>(head).selected_ref() == small_ref);
}

TEST_CASE("a candidate outside the list is refused", "[variant]") {
    variant_fixture fx;

    const auto res  = fx.instantiate(make_prefab());
    const auto head = res.name_to_entity.at("head");

    const auto result = fx.w.system<variant_system>().apply(head, fx.library, 7);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == variant_error::out_of_range);
}

// Поддерево ставится инстанцированием, а не подменой объёма, и системе оно не по
// зубам: про такого кандидата слот знает, но ставит его тот, у кого есть
// разборщик.
TEST_CASE("a subtree candidate is not a volume swap", "[variant]") {
    variant_fixture fx;

    auto prefab = make_prefab();
    prefab.entities.front().tags.back().set_prop("candidate", "prefabs/m_head_orc.vox");

    const auto res  = fx.instantiate(prefab);
    const auto head = res.name_to_entity.at("head");

    const auto result = fx.w.system<variant_system>().apply(head, fx.library, 2);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == variant_error::unsupported_kind);
}
