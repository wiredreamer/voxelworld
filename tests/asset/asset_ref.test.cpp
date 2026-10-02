#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

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

TEST_CASE("a model without a ref lands in the models folder of its prefab", "[asset_ref]") {
    const auto ref = asset::default_model_ref(asset::asset_ref{"prefabs/p_human.vox"}, "body");

    REQUIRE(ref == asset::asset_ref{"models/p_human/m_body.voxm"});
}

TEST_CASE("a model of a prefab moves into the folder of another prefab", "[asset_ref]") {
    const asset::asset_ref from{"prefabs/m_human.vox"};
    const asset::asset_ref to{"prefabs/m_orc.vox"};

    REQUIRE(
        asset::rehomed_model_ref(asset::asset_ref{"models/m_human/body.voxm"}, from, to) ==
        asset::asset_ref{"models/m_orc/body.voxm"}
    );
    REQUIRE(
        asset::rehomed_model_ref(asset::asset_ref{"models/m_human/armor/chest.voxm"}, from, to) ==
        asset::asset_ref{"models/m_orc/armor/chest.voxm"}
    );
}

TEST_CASE("a model of another prefab stays where it is", "[asset_ref]") {
    const asset::asset_ref from{"prefabs/m_human.vox"};
    const asset::asset_ref to{"prefabs/m_orc.vox"};

    REQUIRE_FALSE(
        asset::rehomed_model_ref(asset::asset_ref{"models/m_sword/root.voxm"}, from, to)
            .has_value()
    );
    REQUIRE_FALSE(
        asset::rehomed_model_ref(asset::asset_ref{"models/m_human_old/body.voxm"}, from, to)
            .has_value()
    );
    REQUIRE_FALSE(
        asset::rehomed_model_ref(
            asset::asset_ref{"models/m_human/body.voxm"}, asset::asset_ref{}, to
        )
            .has_value()
    );
}

TEST_CASE("a renamed model keeps its folder and extension", "[asset_ref]") {
    REQUIRE(
        asset::renamed_model_ref(asset::asset_ref{"models/m_human/body.voxm"}, "torso") ==
        asset::asset_ref{"models/m_human/torso.voxm"}
    );
    REQUIRE(
        asset::renamed_model_ref(asset::asset_ref{"models/m_human/armor/chest.voxm"}, "plate") ==
        asset::asset_ref{"models/m_human/armor/plate.voxm"}
    );
    REQUIRE(
        asset::renamed_model_ref(asset::asset_ref{"body.voxm"}, "torso") ==
        asset::asset_ref{"torso.voxm"}
    );
}

TEST_CASE("a model does not take a name that would leave its folder", "[asset_ref]") {
    const asset::asset_ref model{"models/m_human/body.voxm"};

    REQUIRE_FALSE(asset::renamed_model_ref(model, "").has_value());
    REQUIRE_FALSE(asset::renamed_model_ref(model, "armor/chest").has_value());
    REQUIRE_FALSE(asset::renamed_model_ref(model, "..\\chest").has_value());
    REQUIRE_FALSE(asset::renamed_model_ref(model, "torso.voxm").has_value());
    REQUIRE_FALSE(asset::renamed_model_ref(asset::asset_ref{}, "torso").has_value());
}

TEST_CASE("the folder of a prefab does not leak into its model refs", "[asset_ref]") {
    REQUIRE(
        asset::default_model_ref(asset::asset_ref{"m_human.vox"}, "body") ==
        asset::default_model_ref(asset::asset_ref{"prefabs/enemies/m_human.vox"}, "body")
    );
}
