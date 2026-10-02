module vw.game;

import std;
import vw.core;
import vw.ecs;
import vw.world;

namespace vw::game {

surface_placement_system::surface_placement_system(
    ecs::world& w
)
    : world_{&w} {}

auto surface_placement_system::place(
    ecs::entity ent, const vec2f& world_xz, float32 clearance_voxels
) -> void {
    surface_placement_component request;
    request.world_xz_         = world_xz;
    request.clearance_voxels_ = clearance_voxels;

    world_->modify(ent).with<surface_placement_component>(std::move(request));
}

auto surface_placement_system::is_waiting(
    ecs::entity ent
) const -> bool {
    return world_->has<surface_placement_component>(ent);
}

auto surface_placement_system::update(
    float32
) -> void {
    const auto* grid = world_->system<ecs::world_grid_system>().grid();
    if (grid == nullptr) {
        return;
    }

    const auto units_per_voxel = static_cast<float32>(grid->world_units_per_voxel());
    auto& transforms           = world_->system<ecs::transform_system>();

    placed_.clear();

    world_->for_each<surface_placement_component>(
        [&](ecs::entity ent, const surface_placement_component& request) {
            const vec2f at = request.world_xz_;

            const auto surface = grid->get_surface_voxel_y(
                static_cast<int32>(at.x / units_per_voxel),
                static_cast<int32>(at.y / units_per_voxel)
            );
            if (!surface) {
                return;
            }

            const float32 y =
                (static_cast<float32>(*surface) + request.clearance_voxels_) * units_per_voxel;

            transforms.modify(ent).set_position({at.x, y, at.y});
            placed_.push_back(ent);
        }
    );

    for (const ecs::entity ent : placed_) {
        world_->modify(ent).without<surface_placement_component>();
    }
}

}  // namespace vw::game
