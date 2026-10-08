#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

constexpr material wood{7};
constexpr material leaf{9};

}  // namespace

TEST_CASE("a model without materials answers inert everywhere", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::gray[8], material{});

    REQUIRE(m.get_material(3, 4, 5) == material{});
    REQUIRE(m.get_material(40, 40, 40) == material{});
    REQUIRE(m.get_page_material(0, 0, 0) == material{});
}

TEST_CASE("a voxel keeps the material it was set with and loses it when cleared", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    m.set_voxel(4, 4, 5, voxels::amber[4], leaf);
    m.set_voxel(60, 60, 60, voxels::green[3], leaf);

    REQUIRE(m.get_material(3, 4, 5) == wood);
    REQUIRE(m.get_material(4, 4, 5) == leaf);
    REQUIRE(m.get_material(5, 4, 5) == material{});
    REQUIRE(m.get_material(60, 60, 60) == leaf);
    REQUIRE_FALSE(m.get_page_material(0, 0, 0).has_value());

    m.set_voxel(4, 4, 5, voxels::air, leaf);
    REQUIRE(m.get_material(4, 4, 5) == material{});
}

TEST_CASE("a page of one material folds to a single value and unfolds on a different one", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    for (int32 z = 8; z < 16; ++z) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 x = 16; x < 24; ++x) {
                m.set_voxel(x, y, z, voxels::green[3], leaf);
            }
        }
    }
    REQUIRE_FALSE(m.get_page_material(2, 0, 1).has_value());

    static_cast<void>(m.compact_pages());
    REQUIRE(m.get_page_material(2, 0, 1) == leaf);
    REQUIRE(m.get_material(20, 3, 12) == leaf);

    m.set_voxel(20, 3, 12, voxels::amber[4], wood);
    REQUIRE_FALSE(m.get_page_material(2, 0, 1).has_value());
    REQUIRE(m.get_material(20, 3, 12) == wood);
    REQUIRE(m.get_material(21, 3, 12) == leaf);
}

TEST_CASE("clearing the last material drops the layer", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    REQUIRE_FALSE(m.get_page_material(0, 0, 0).has_value());

    m.set_voxel(3, 4, 5, voxels::air);
    static_cast<void>(m.compact_pages());

    REQUIRE(m.get_page_material(0, 0, 0) == material{});
    REQUIRE(m.get_page_material(7, 7, 7) == material{});
}

TEST_CASE("a clone carries the materials of its source", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model source{ids, pages, side, side, side};
    source.set_voxel(3, 4, 5, voxels::amber[4], wood);
    source.set_voxel(33, 4, 5, voxels::green[3], leaf);

    asset::model copy{ids, pages, side, side, side};
    copy.clone_pages_from(source);

    REQUIRE(copy.get_material(3, 4, 5) == wood);
    REQUIRE(copy.get_material(33, 4, 5) == leaf);

    source.set_voxel(3, 4, 5, voxels::amber[4], leaf);
    REQUIRE(copy.get_material(3, 4, 5) == wood);
}

TEST_CASE("rows of a material set name exactly its voxels", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    m.set_voxel(4, 4, 5, voxels::green[3], leaf);
    m.set_voxel(63, 63, 63, voxels::green[3], leaf);

    material_set wanted;
    wanted.set(leaf.value);

    asset::chunk_occupancy rows;
    REQUIRE(m.build_rows_of(rows, wanted));

    REQUIRE(rows.test(4, 4, 5));
    REQUIRE(rows.test(63, 63, 63));
    REQUIRE_FALSE(rows.test(3, 4, 5));
    REQUIRE(((rows.zrow(4, 4) >> 5) & 1U) == 1);
    REQUIRE(((rows.zrow(4, 3) >> 5) & 1U) == 0);

    asset::face_occupancy plane;
    asset::face_occupancy held;
    REQUIRE(m.extract_face(face_direction::pos_x, plane, wanted, held));
    REQUIRE(plane.test(63, 63));
    REQUIRE(held.test(63, 63));
}

namespace {

constexpr matter trunk{voxels::amber[4], materials::wood};
constexpr matter crown{voxels::green[3], materials::leaves};
constexpr matter ember{voxels::red[8], materials::fire};

auto small_tree(asset::model_registry& registry) -> std::shared_ptr<asset::model> {
    auto tree = registry.create_unnamed(vec3i{6, 8, 6});
    tree->set_voxel(vec3i{2, 1, 2}, trunk);
    tree->set_voxel(vec3i{2, 2, 2}, trunk);
    tree->set_voxel(vec3i{2, 3, 2}, crown);
    tree->set_voxel(vec3i{3, 3, 2}, crown);
    tree->set_voxel(vec3i{2, 3, 3}, voxels::gray[8]);
    return tree;
}

}  // namespace

TEST_CASE("a voxel set without a material is inert", "[model][material]") {
    asset::model_registry registry;

    auto volume = registry.create_unnamed(vec3i{4, 4, 4});
    volume->set_voxel(vec3i{1, 1, 1}, crown);
    volume->set_voxel(vec3i{1, 1, 1}, voxels::green[3]);

    REQUIRE(volume->get_matter(1, 1, 1) == matter{voxels::green[3], materials::inert});
}

TEST_CASE("a writer and a batch carry the material into the volume", "[model][material]") {
    asset::model_registry registry;

    auto volume = registry.create_unnamed(vec3i{16, 16, 16});
    {
        asset::model_writer writer{*volume};
        writer.set(1, 2, 3, ember);
        writer.fill_page(1, 0, 0, crown);
    }

    asset::voxel_batch batch;
    batch.set(vec3i{4, 4, 4}, trunk);
    batch.fill_page(vec3i{0, 1, 1}, ember);
    batch.apply_to(*volume);

    REQUIRE(volume->get_matter(1, 2, 3) == ember);
    REQUIRE(volume->get_matter(8, 0, 0) == crown);
    REQUIRE(volume->get_matter(15, 7, 7) == crown);
    REQUIRE(volume->get_page_material(1, 0, 0) == materials::leaves);
    REQUIRE(volume->get_matter(4, 4, 4) == trunk);
    REQUIRE(volume->get_matter(3, 12, 12) == ember);
}

TEST_CASE("filling a whole model sets one material everywhere", "[model][material]") {
    asset::model_registry registry;

    auto volume = registry.create_unnamed(vec3i{16, 8, 8});
    volume->fill(crown);
    REQUIRE(volume->get_matter(0, 0, 0) == crown);
    REQUIRE(volume->get_matter(15, 7, 7) == crown);

    volume->fill(voxels::air);
    REQUIRE(volume->get_matter(3, 3, 3) == matter{});
    REQUIRE(volume->get_page_material(0, 0, 0) == materials::inert);
}

TEST_CASE("trimming, turning and resizing keep each voxel's material", "[model][material]") {
    asset::model_registry registry;
    const auto tree = small_tree(registry);

    const auto tight = asset::trimmed(*tree, registry);
    REQUIRE(tight != nullptr);
    REQUIRE(tight->size() == vec3i{2, 3, 2});
    REQUIRE(tight->get_matter(0, 0, 0) == trunk);
    REQUIRE(tight->get_matter(0, 2, 0) == crown);
    REQUIRE(tight->get_matter(1, 2, 0) == crown);
    REQUIRE(tight->get_matter(0, 2, 1) == matter{voxels::gray[8]});

    const auto mirrored =
        asset::reoriented(*tree, asset::mirrored_orientation(asset::voxel_axis::x), registry);
    REQUIRE(mirrored->get_matter(3, 1, 2) == trunk);
    REQUIRE(mirrored->get_matter(2, 3, 2) == crown);
    REQUIRE(mirrored->get_matter(3, 3, 3) == matter{voxels::gray[8]});

    const auto grown = asset::resized(*tree, vec3i{1, 0, 2}, vec3i{0, 0, 0}, registry);
    REQUIRE(grown->size() == vec3i{7, 8, 8});
    REQUIRE(grown->get_matter(3, 1, 4) == trunk);
    REQUIRE(grown->get_matter(4, 3, 4) == crown);
}

TEST_CASE("erasing and editing leave the materials of untouched voxels alone", "[model][material]") {
    asset::model_registry registry;
    const auto tree = small_tree(registry);

    const auto topped = asset::erased(
        *tree, asset::voxel_bounds{.min = vec3i{0, 3, 0}, .max = vec3i{5, 7, 5}}, registry
    );
    REQUIRE(topped->get_matter(2, 1, 2) == trunk);
    REQUIRE(topped->get_matter(2, 2, 2) == trunk);
    REQUIRE(topped->get_matter(2, 3, 2) == matter{});

    const std::array edits{
        asset::voxel_edit{.position = vec3i{0, 0, 0}, .value = ember},
        asset::voxel_edit{.position = vec3i{2, 2, 2}, .value = voxels::air},
    };
    const auto changed = asset::edited(*tree, edits, registry);
    REQUIRE(changed->get_matter(0, 0, 0) == ember);
    REQUIRE(changed->get_matter(2, 2, 2) == matter{});
    REQUIRE(changed->get_matter(2, 1, 2) == trunk);
    REQUIRE(changed->get_matter(3, 3, 2) == crown);
}

TEST_CASE("repainting solid voxels keeps their material, filling every cell replaces it", "[model][material]") {
    asset::model_registry registry;
    const auto tree = small_tree(registry);

    const asset::voxel_bounds everything{.min = vec3i{0, 0, 0}, .max = vec3i{5, 7, 5}};

    const auto repainted =
        asset::filled(*tree, everything, voxels::blue[5], asset::fill_scope::solid_only, registry);
    REQUIRE(repainted->get_matter(2, 1, 2) == matter{voxels::blue[5], materials::wood});
    REQUIRE(repainted->get_matter(3, 3, 2) == matter{voxels::blue[5], materials::leaves});
    REQUIRE(repainted->get_matter(2, 3, 3) == matter{voxels::blue[5], materials::inert});
    REQUIRE(repainted->get_matter(0, 0, 0) == matter{});

    const auto flooded =
        asset::filled(*tree, everything, ember, asset::fill_scope::every_cell, registry);
    REQUIRE(flooded->get_matter(2, 1, 2) == ember);
    REQUIRE(flooded->get_matter(0, 0, 0) == ember);
}

TEST_CASE("a clip copied from a volume pastes its materials back", "[model][material]") {
    asset::model_registry registry;
    const auto tree = small_tree(registry);

    const auto clip =
        asset::copied(*tree, asset::voxel_bounds{.min = vec3i{2, 1, 2}, .max = vec3i{3, 3, 3}});
    REQUIRE(clip.size == vec3i{2, 3, 2});
    REQUIRE(clip.at(vec3i{0, 0, 0}) == trunk);
    REQUIRE(clip.at(vec3i{1, 2, 0}) == crown);
    REQUIRE(clip.at(vec3i{1, 0, 0}) == matter{});

    const auto turned = asset::reoriented(clip, asset::mirrored_orientation(asset::voxel_axis::x));
    REQUIRE(turned.at(vec3i{1, 0, 0}) == trunk);
    REQUIRE(turned.at(vec3i{0, 2, 0}) == crown);

    auto target = registry.create_unnamed(vec3i{6, 8, 6});
    target->set_voxel(vec3i{0, 0, 0}, ember);

    const auto result = asset::pasted(*target, clip, vec3i{1, 0, 1}, asset::paste_mode::keep_air, registry);
    REQUIRE(result->get_matter(1, 0, 1) == trunk);
    REQUIRE(result->get_matter(1, 1, 1) == trunk);
    REQUIRE(result->get_matter(2, 2, 1) == crown);
    REQUIRE(result->get_matter(1, 2, 2) == matter{voxels::gray[8]});
    REQUIRE(result->get_matter(0, 0, 0) == ember);
}
