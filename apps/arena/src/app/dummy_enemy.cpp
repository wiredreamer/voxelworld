module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;
import vw.platform;
import vw.gfx;

namespace vw::arena {

dummy_enemy::dummy_enemy(
    gfx::engine& engine, const vec2f& spawn_xz
)
    : engine_{engine}, spawn_xz_{spawn_xz} {
    auto& world         = engine_.get_world();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& physics_sys   = world.system<ecs::physics_system>();
    auto& model_sys     = world.system<ecs::model_system>();
    auto& spatial_sys   = world.system<ecs::spatial_system>();

    ent_ =
        world.create()
            .with<ecs::hierarchy_component>()
            .with<ecs::transform_component>()
            .with<ecs::spatial_component>()
            .with<ecs::rigid_body_component>()
            .with<ecs::box_collider_component>()
            .with<ecs::model_component>()
            .get_entity();

    transform_sys
        .modify(ent_)  //
        .set_position({spawn_xz_.x, 500.0f, spawn_xz_.y});

    physics_sys
        .modify_collider(ent_)  //
        .set_extents({16.0f, 32.0f, 16.0f})
        .set_offset({0.0f, 0.0f, 0.0f});

    spatial_sys  //
        .modify(ent_)
        .set_layer(ecs::spatial_layer::character);

    model_sys.modify(ent_).set_model(create_model());

    world.system<game::surface_placement_system>().place(ent_, spawn_xz_);
}

dummy_enemy::~dummy_enemy() {
    if (ent_.is_valid()) {
        engine_.get_world().destroy(ent_);
    }
}

auto dummy_enemy::get_entity() const -> ecs::entity {
    return ent_;
}

auto dummy_enemy::create_model() -> std::shared_ptr<asset::model> {
    auto& model_reg = engine_.get_world().resource<asset::model_registry>();
    auto model      = model_reg.create_unnamed(16, 32, 16);
    model->fill(voxels::red[3]);
    model->set_pivot({8.0f, 16.0f, 8.0f});
    return model;
}

}  // namespace vw::arena
