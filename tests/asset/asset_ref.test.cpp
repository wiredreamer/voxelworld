#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

// Ссылка — ключ кеша, поэтому два написания одного пути обязаны дать одну
// запись: иначе один объём загрузится дважды и правки разойдутся по копиям.
TEST_CASE("an asset ref normalises the way a path is spelled", "[asset_ref]") {
    REQUIRE(asset::asset_ref{"./models/body.voxm"} == asset::asset_ref{"models/body.voxm"});
    REQUIRE(asset::asset_ref{"/models/body.voxm"} == asset::asset_ref{"models/body.voxm"});
    REQUIRE(asset::asset_ref{"models\\body.voxm"} == asset::asset_ref{"models/body.voxm"});
}

TEST_CASE("an asset ref tells its extension and stem apart", "[asset_ref]") {
    const asset::asset_ref ref{"models/m_human/body.voxm"};

    REQUIRE(ref.extension() == ".voxm");
    REQUIRE(ref.stem() == "body");
    REQUIRE_FALSE(ref.empty());
    REQUIRE(asset::asset_ref{}.empty());
}

TEST_CASE("an asset ref without an extension reports none", "[asset_ref]") {
    const asset::asset_ref ref{"models/body"};

    REQUIRE(ref.extension().empty());
    REQUIRE(ref.stem() == "body");
}

// Правило имени общее для конвертации старых .vox и для первой записи нового
// узла. Разойдись они — один объём получил бы два файла.
TEST_CASE("a model without a ref lands in the models folder of its prefab", "[asset_ref]") {
    const auto ref = asset::default_model_ref(asset::asset_ref{"prefabs/m_human.vox"}, "body");

    REQUIRE(ref == asset::asset_ref{"models/m_human/body.voxm"});
}

// Объёмы лежат в своём каталоге, а не рядом с деревом, поэтому от префаба
// остаётся только имя: откуда его открыли, на раскладку не влияет.
TEST_CASE("the folder of a prefab does not leak into its model refs", "[asset_ref]") {
    REQUIRE(
        asset::default_model_ref(asset::asset_ref{"m_human.vox"}, "body") ==
        asset::default_model_ref(asset::asset_ref{"prefabs/enemies/m_human.vox"}, "body")
    );
}
