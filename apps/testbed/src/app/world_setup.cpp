module vw.testbed;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::testbed {

auto testbed_app::setup_world_grid() -> void {
    auto& world = get_engine().get_world();

    generator_params_ = {
        .world_units_per_voxel = 8,
    };
    auto& registry = world.resource<asset::model_registry>();
    auto generator = std::make_unique<ecs::perlin_terrain_generator>(
        registry.get_identity_pool(), registry.get_page_pool(), generator_params_);
    generator_  = generator.get();
    auto grid   = std::make_unique<ecs::world_grid>(
        world, generator_params_.world_units_per_voxel
    );
    world_grid_  = grid.get();
    auto loader  = std::make_unique<ecs::chunk_loader>(
        std::move(generator), get_engine().get_jobs());
    auto& gs     = world.system<ecs::world_grid_system>();
    gs.set_grid(std::move(grid));
    gs.set_loader(std::move(loader), get_engine().get_jobs());

    viewer_ = world.create()
        .with<ecs::transform_component>()
        .with<ecs::world_view_component>()
        .get_entity();

    gs.modify_view(viewer_).set_view_distance(static_cast<int32>(view_distance_));

    const auto chunk_units = static_cast<float32>(
        ecs::chunk::size * generator_params_.world_units_per_voxel
    );

    auto& lod = world.system<ecs::lod_system>();
    lod.set_default_base_distance(lod_distance_ * chunk_units);
    lod.set_forced_level(lod_level_);
}

auto testbed_app::try_place_camera() -> void {
    if (camera_placed_) {
        return;
    }

    for (auto column : {vec2i{0, 0}, vec2i{-1, 0}, vec2i{0, -1}, vec2i{-1, -1}}) {
        if (!world_grid_->has_column(column)) {
            return;
        }
    }

    constexpr int32 probe_radius = 48;
    constexpr int32 probe_step   = 4;
    constexpr int32 eye_height   = 6;

    std::optional<int32> highest;
    for (int32 x = -probe_radius; x <= probe_radius; x += probe_step) {
        for (int32 z = -probe_radius; z <= probe_radius; z += probe_step) {
            if (const auto h = world_grid_->get_surface_voxel_y(x, z)) {
                highest = highest ? std::max(*highest, *h) : *h;
            }
        }
    }

    if (!highest) {
        return;
    }

    auto scale    = static_cast<float32>(generator_params_.world_units_per_voxel);
    float32 cam_y = static_cast<float32>(*highest + eye_height) * scale;
    get_engine().get_camera().set_position({0.0f, cam_y, 0.0f});
    bench_altitude_ = cam_y;
    camera_placed_  = true;

    log::info(
        "camera at y {} -- {} voxels over the highest ground near the origin, which is {}",
        cam_y, eye_height, *highest
    );

}

[[nodiscard]] auto testbed_app::streaming_settled() const -> bool {
    const auto& stats = get_engine().get_world().system<ecs::world_grid_system>().get_stats();
    return stats.pending_count == 0 && stats.lighting_count == 0 &&
           get_engine().get_renderer().get_mesh_pool().get_pending_count() == 0;
}

}  // namespace vw::testbed
