module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::arena {

player::player(
    gfx::engine& engine, asset::asset_storage& assets
)
    : engine_{engine}, assets_{assets} {
    auto& world         = engine_.get_world();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& physics_sys   = world.system<ecs::physics_system>();

    root_ = world.create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .with<ecs::rigid_body_component>()
        .with<ecs::box_collider_component>()
        .with<ecs::character_controller_component>()
        .with<ecs::movement_intent_component>()
        .with<ecs::world_view_component>()
        .with<ecs::animation_player_component>()
        .with<ecs::animation_fsm_component>()
        .get_entity();

    transform_sys.modify(root_).set_position({0.0f, 500.0f, 0.0f});

    physics_sys.modify_collider(root_)
        .set_extents({12.0f, 28.0f, 12.0f})
        .set_offset({0.0f, 2.0f, 0.0f});

    world.system<ecs::spatial_system>().modify(root_).set_layer(ecs::spatial_layer::character);

    body_       = create_body_part("m_human", "body");
    head_       = create_body_part("m_human", "head");
    hand_right_ = create_body_part("m_human", "hand_right");
    hand_left_  = create_body_part("m_human", "hand_left");
    foot_right_ = create_body_part("m_human", "foot_right");
    foot_left_  = create_body_part("m_human", "foot_left");

    attach_machines_();
}

auto player::attach_machines_() const -> void {
    const auto* prefab = assets_.get_prefab("m_human");
    if (prefab == nullptr) {
        return;
    }

    auto& world = engine_.get_world();

    for (std::size_t layer = 0; layer < prefab->fsm_refs.size(); ++layer) {
        const auto* data = assets_.get_fsm(prefab->fsm_refs[layer]);
        if (data == nullptr) {
            continue;
        }

        const auto machines = world.system<ecs::animation_fsm_system>().modify(root_);
        machines.add_machine(layer, assets_.make_fsm(*data));
        machines.declare_parameters(*data);
    }
}

player::~player() {
    auto& world = engine_.get_world();
    if (sword_.is_valid())       world.destroy(sword_);
    if (foot_left_.is_valid())   world.destroy(foot_left_);
    if (foot_right_.is_valid())  world.destroy(foot_right_);
    if (hand_left_.is_valid())   world.destroy(hand_left_);
    if (hand_right_.is_valid())  world.destroy(hand_right_);
    if (head_.is_valid())        world.destroy(head_);
    if (body_.is_valid())        world.destroy(body_);
    if (root_.is_valid())        world.destroy(root_);
}

auto player::update(
    const gfx::player_input_state& input
) -> void {
    const auto ent = root_;
    auto& world    = engine_.get_world();
    auto& camera   = engine_.get_camera();
    const auto& rb = world.get<ecs::rigid_body_component>(ent);

    auto forward = camera.get_forward();
    forward.y    = 0.0f;
    forward      = math::normalize(forward);

    auto right = camera.get_right();
    right.y    = 0.0f;
    right      = math::normalize(right);

    const vec3f move_dir =
        math::normalize(forward * input.move_input.x + right * input.move_input.y);

    auto modifier = world.system<ecs::character_controller_system>().modify(ent);
    modifier.set_move_input(move_dir);

    const auto& anim_player  = world.get<ecs::animation_player_component>(ent);
    const auto& action_layer = anim_player.get_layer(1);
    is_attacking_            = action_layer.is_active();

    if (is_attacking_) {
        modifier.set_facing_direction(attack_facing_);
        modifier.set_rotation_speed(attack_rotation_speed_);
    } else {
        modifier.set_rotation_speed(default_rotation_speed_);
        if (math::length(move_dir) > math::epsilon) {
            modifier.set_facing_direction(move_dir);
        }
    }

    if (input.attack_requested && can_attack()) {
        attack_facing_ = forward;
        handle_attack();
    }

    if (input.jump_requested) {
        modifier.request_jump();
    }

    if (input.jump_requested && rb.is_grounded()) {
        need_update_jump_ = true;
    }

    if (need_update_jump_ && !rb.is_grounded()) {
        jump_counter_     = (jump_counter_ + 1) % 2;
        need_update_jump_ = false;
    }

    world.system<ecs::animation_fsm_system>().modify(ent).set_parameter(
        "jump_count", static_cast<float32>(jump_counter_)
    );
}

auto player::try_place(
    float32 world_units_per_voxel
) -> void {
    if (placed_) {
        return;
    }

    const auto grid    = engine_.get_world().system<ecs::world_grid_system>().grid();
    const auto surface = grid->get_surface_voxel_y(0, 0);
    if (!surface) {
        return;
    }

    float32 spawn_y = (static_cast<float32>(*surface) + 6.0f) * world_units_per_voxel;

    engine_.get_world()
        .system<ecs::transform_system>()
        .modify(root_)
        .set_position({0.0f, spawn_y, 0.0f});

    placed_ = true;
}

auto player::toggle_sword() -> void {
    if (!hand_right_.is_valid()) {
        return;
    }

    auto& world         = engine_.get_world();
    const auto hand_ent = hand_right_;

    if (sword_.is_valid()) {
        world.system<ecs::socket_system>().modify(hand_ent).detach("hand_right");
        world.destroy(sword_);
        sword_ = ecs::invalid_entity;
    } else {
        sword_ = world.create()
            .with<ecs::hierarchy_component>()
            .with<ecs::transform_component>()
            .with<ecs::spatial_component>()
            .with<ecs::model_component>()
            .get_entity();

        world.system<ecs::model_system>().modify(sword_).set_model(assets_.get_model("m_sword", "root"));
        world.system<ecs::socket_system>().modify(hand_ent).attach("hand_right", sword_);
        world.system<ecs::spatial_system>().modify(sword_).set_layer(ecs::spatial_layer::character);
    }
}

auto player::handle_attack() const -> void {
    engine_.get_world()
        .system<ecs::animation_fsm_system>()
        .modify(root_)
        .fire_trigger("attack");
}

auto player::can_attack() const -> bool {
    const auto& anim_player =
        engine_.get_world().get<ecs::animation_player_component>(root_);
    return sword_.is_valid() && !anim_player.get_layer(1).is_active();
}

auto player::get_entity() const -> ecs::entity {
    return root_;
}

auto player::has_sword() const -> bool {
    return sword_.is_valid();
}

auto player::is_placed() const -> bool {
    return placed_;
}

auto player::create_body_part(
    std::string_view prefab_name, std::string_view part_name
) const -> ecs::entity {
    auto& world = engine_.get_world();

    const auto ent = world.create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .get_entity();

    world.system<ecs::hierarchy_system>().modify(ent).set_parent(root_);

    const auto& ent_data = assets_.get_entity(prefab_name, part_name);
    ecs::apply_node(world, ent, ent_data, assets_.library());

    return ent;
}

}  // namespace vw::arena
