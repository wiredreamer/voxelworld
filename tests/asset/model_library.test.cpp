#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

class temp_root final {
public:
    explicit temp_root(std::string_view name)
        : path_(std::filesystem::temp_directory_path() / std::format("vw_{}", name)) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~temp_root() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    temp_root(const temp_root&)                    = delete;
    auto operator=(const temp_root&) -> temp_root& = delete;

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("a model survives a round trip through the library", "[library]") {
    const temp_root root{"library_round_trip"};
    asset::model_registry registry;
    const voxel_registry voxel_types;
    asset::model_library library{registry, voxel_types, root.path()};

    auto source = registry.create_unnamed(voxels::world::category, vec3i{4, 4, 4});
    source->set_voxel(1, 2, 3, voxels::world::grass[0]);
    source->set_pivot(vec3f{1.5F, 2.5F, 3.5F});

    const asset::asset_ref ref{"models/m_human/body.voxm"};
    REQUIRE(library.save(ref, *source).has_value());

    REQUIRE(std::filesystem::exists(root.path() / "models/m_human/body.voxm"));

    asset::model_registry other_registry;
    asset::model_library other{other_registry, voxel_types, root.path()};

    const auto restored = other.load(ref);
    REQUIRE(restored.has_value());
    REQUIRE((*restored)->size() == vec3i{4, 4, 4});
    REQUIRE((*restored)->get_voxel(1, 2, 3) == voxels::world::grass[0]);
    REQUIRE((*restored)->pivot() == vec3f{1.5F, 2.5F, 3.5F});
}

TEST_CASE("the library hands out one model per ref", "[library]") {
    const temp_root root{"library_dedup"};
    asset::model_registry registry;
    const voxel_registry voxel_types;
    asset::model_library library{registry, voxel_types, root.path()};

    auto source = registry.create_unnamed(voxels::world::category, vec3i{2, 2, 2});
    const asset::asset_ref ref{"models/head.voxm"};
    REQUIRE(library.save(ref, *source).has_value());

    const auto first  = library.load(ref);
    const auto second = library.load(ref);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(*first == *second);
}

TEST_CASE("a missing model is an error, not a crash", "[library]") {
    const temp_root root{"library_missing"};
    asset::model_registry registry;
    const voxel_registry voxel_types;
    asset::model_library library{registry, voxel_types, root.path()};

    const auto missing = library.load(asset::asset_ref{"models/nothing.voxm"});

    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error() == asset::voxm_deserializer::error_type::file_open_failed);
}

TEST_CASE("an adopted model answers the next lookup", "[library]") {
    const temp_root root{"library_adopt"};
    asset::model_registry registry;
    const voxel_registry voxel_types;
    asset::model_library library{registry, voxel_types, root.path()};

    auto source = registry.create_unnamed(voxels::world::category, vec3i{2, 2, 2});
    const asset::asset_ref ref{"models/hand.voxm"};

    REQUIRE(library.find(ref) == nullptr);
    library.adopt(ref, source);
    REQUIRE(library.find(ref) == source);

    const auto loaded = library.load(ref);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == source);
}
