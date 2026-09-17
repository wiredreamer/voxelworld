#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

namespace fs = std::filesystem;

struct temp_assets final {
    fs::path root;

    explicit temp_assets(std::string_view name)
        : root(fs::temp_directory_path() / std::format("vw_variant_{}", name)) {
        fs::remove_all(root);
        fs::create_directories(root / "prefabs");
    }

    ~temp_assets() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    temp_assets(const temp_assets&)                    = delete;
    auto operator=(const temp_assets&) -> temp_assets& = delete;

    auto write(std::string_view relative, std::string_view text) const -> void {
        std::ofstream file(root / fs::path{relative});
        file << text;
    }
};

constexpr std::string_view orc_head =
    "# Vox File Version 4.0\n"
    "root head_orc\n"
    "entity head_orc\n"
    "\ttransform 0 0 0\t0 0 0\t1 1 1\n"
    "\tanim_target head\n"
    "\tsocket helmet\n"
    "\t\tpos 0 2 0\n"
    "\t\trot 0 0 0\n"
    "\t\tscale 1 1 1\n";

constexpr std::string_view bare_head =
    "# Vox File Version 4.0\n"
    "root head_bare\n"
    "entity head_bare\n"
    "\ttransform 0 0 0\t0 0 0\t1 1 1\n"
    "\tanim_target head\n";

struct subtree_fixture final {
    temp_assets assets;
    world w;
    voxel_registry voxel_types;
    asset::vox_parser_plain parser;
    asset::model_library library;

    explicit subtree_fixture(std::string_view name)
        : assets(name)
        , library(w.resource<asset::model_registry>(), voxel_types, assets.root.string()) {
        assets.write("prefabs/head_orc.vox", orc_head);
        assets.write("prefabs/head_bare.vox", bare_head);
    }

    [[nodiscard]] auto deserializer() -> vox_deserializer {
        return vox_deserializer{w, parser, library};
    }
};

auto make_prefab() -> asset::vox_prefab_data {
    asset::vox_prefab_data prefab;
    prefab.root_name = "head";

    asset::vox_entity_data head;
    head.name = "head";
    head.add("transform", "0 11 0\t0 0 0\t1 1 1");
    head.add("variant", "head")
        .set_prop("candidate", "prefabs/head_orc.vox")
        .set_prop("candidate", "prefabs/head_bare.vox")
        .set_prop("selected", "0")
        .set_prop("target", "head")
        .set_prop("socket", "helmet");
    prefab.entities.push_back(head);

    return prefab;
}

}  // namespace

TEST_CASE("a subtree candidate hangs under the node", "[variant]") {
    subtree_fixture fx{"put"};

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(make_prefab(), {});
    fx.w.update(0.016F);

    const auto head = res.name_to_entity.at("head");
    REQUIRE(deserializer.put_variant(head, 0).has_value());
    fx.w.update(0.016F);

    const auto& slot = fx.w.get<variant_slot_component>(head);
    REQUIRE(slot.get_content().size() == 1);
    REQUIRE(slot.get_selected() == 0);

    const auto content = slot.get_content().front();
    REQUIRE(fx.w.has<slot_content_component>(content));
    REQUIRE(fx.w.get<slot_content_component>(content).get_owner() == head);
    REQUIRE(fx.w.get<hierarchy_component>(content).get_parent() == head);
    REQUIRE(fx.w.has<socket_component>(content));
}

TEST_CASE("a candidate that does not close the slot is refused", "[variant]") {
    subtree_fixture fx{"contract"};

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(make_prefab(), {});
    fx.w.update(0.016F);

    const auto head = res.name_to_entity.at("head");
    REQUIRE(deserializer.put_variant(head, 0).has_value());
    fx.w.update(0.016F);

    const auto before = fx.w.get<variant_slot_component>(head).get_content();

    const auto result = deserializer.put_variant(head, 1);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == variant_error::contract_unmet);

    const auto& slot = fx.w.get<variant_slot_component>(head);
    REQUIRE(slot.get_selected() == 0);
    REQUIRE(slot.get_content() == before);
}

TEST_CASE("content by reference stays out of the file", "[variant]") {
    subtree_fixture fx{"write"};

    auto deserializer = fx.deserializer();
    const auto res    = deserializer.instantiate(make_prefab(), {});
    fx.w.update(0.016F);

    const auto head = res.name_to_entity.at("head");
    REQUIRE(deserializer.put_variant(head, 0).has_value());
    fx.w.update(0.016F);

    asset::vox_writer_plain writer;
    vox_serializer serializer{fx.w, writer, head, {.entity_names = res.entity_to_name}};

    const auto written = serializer.extract();

    REQUIRE(written.entities.size() == 1);
    REQUIRE(written.entities.front().name == "head");
    REQUIRE(written.entities.front().find("variant") != nullptr);
}
