module vw.arena;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::arena {

dummy_enemy::dummy_enemy(gfx::engine& engine, const vec2f& spawn_xz)
    : engine_{engine}
    , spawn_xz_{spawn_xz} {
    auto& world         = engine_.get_world();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& physics_sys   = world.system<ecs::physics_system>();
    auto& model_sys     = world.system<ecs::model_system>();

    ent_ = world.create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .with<ecs::rigid_body_component>()
        .with<ecs::box_collider_component>()
        .with<ecs::model_component>()
        .get_entity();

    transform_sys.modify(ent_)
        .set_position({spawn_xz_.x, 500.0f, spawn_xz_.y})
        .set_origin({-8.0f, -16.0f, -8.0f});

    physics_sys.modify_collider(ent_)
        .set_extents({16.0f, 32.0f, 16.0f})
        .set_offset({0.0f, 0.0f, 0.0f});

    world.system<ecs::spatial_system>().modify(ent_).set_layer(ecs::spatial_layer::character);

    model_sys.modify(ent_).set_model(create_model());
}

dummy_enemy::~dummy_enemy() {
    if (ent_.is_valid()) {
        engine_.get_world().destroy(ent_);
    }
}

auto dummy_enemy::try_place() -> void {
    if (placed_) {
        return;
    }

    const auto grid = engine_.get_world().system<ecs::world_grid_system>().grid();
    const auto vs   = grid->voxel_scale();
    const auto surface = grid->get_surface_y(
        static_cast<int32>(spawn_xz_.x / vs),
        static_cast<int32>(spawn_xz_.y / vs)
    );
    if (!surface) {
        return;
    }

    float32 spawn_y = (static_cast<float32>(*surface) + 6.0f) * vs;

    engine_.get_world()
        .system<ecs::transform_system>()
        .modify(ent_)
        .set_position({spawn_xz_.x, spawn_y, spawn_xz_.y});

    placed_ = true;
}

auto dummy_enemy::get_entity() const -> ecs::entity {
    return ent_;
}

auto dummy_enemy::is_placed() const -> bool {
    return placed_;
}

auto dummy_enemy::create_model() -> std::shared_ptr<asset::model> {
    auto& model_reg = engine_.get_world().resource<asset::model_registry>();
    auto model = model_reg.create_unnamed(blocks::creature::category, 16, 32, 16);
    model->fill(voxel{blocks::creature::cloth_red[1]});
    return model;
}

}  // namespace vw::arena
