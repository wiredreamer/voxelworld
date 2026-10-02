module vw.game;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

namespace vw::game {
namespace {

constexpr std::string_view body_prefab   = "p_humanoid";
constexpr std::string_view weapon_prefab = "p_sword";
constexpr std::string_view weapon_node   = "root";
constexpr std::string_view weapon_socket = "hand_right";

constexpr std::size_t action_layer = 1;

constexpr float32 default_rotation_speed = 5.0f;
constexpr float32 attack_rotation_speed  = 25.0f;

}  // namespace

player_system::player_system(
    ecs::world& w, asset::asset_storage& assets
)
    : world_{&w}, assets_{&assets} {}

auto player_system::spawn() -> ecs::entity {
    const auto root = world_->create()
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

    world_->system<ecs::physics_system>()
        .modify_collider(root)
        .set_extents({12.0f, 28.0f, 12.0f})
        .set_offset({0.0f, 1.0f, 0.0f});

    world_->system<ecs::spatial_system>().modify(root).set_layer(ecs::spatial_layer::character);

    player_component player;
    player.body_       = create_body_part_(root, "body");
    player.head_       = create_body_part_(root, "head");
    player.hand_right_ = create_body_part_(root, "hand_right");
    player.hand_left_  = create_body_part_(root, "hand_left");
    player.foot_right_ = create_body_part_(root, "foot_right");
    player.foot_left_  = create_body_part_(root, "foot_left");

    attach_machines_(root);

    world_->modify(root).with<player_component>(std::move(player));
    return root;
}

auto player_system::create_body_part_(
    ecs::entity root, std::string_view part_name
) const -> ecs::entity {
    const auto ent = world_->create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .get_entity();

    world_->system<ecs::hierarchy_system>().modify(ent).set_parent(root);

    const auto& node = assets_->get_entity(body_prefab, part_name);
    ecs::apply_node(*world_, ent, node, assets_->library());

    return ent;
}

auto player_system::attach_machines_(
    ecs::entity root
) const -> void {
    const auto* prefab = assets_->get_prefab(body_prefab);
    if (prefab == nullptr) {
        return;
    }

    for (std::size_t layer = 0; layer < prefab->fsm_refs.size(); ++layer) {
        const auto* data = assets_->get_fsm(prefab->fsm_refs[layer]);
        if (data == nullptr) {
            continue;
        }

        const auto machines = world_->system<ecs::animation_fsm_system>().modify(root);
        machines.add_machine(layer, assets_->make_fsm(*data));
        machines.declare_parameters(*data);
    }
}

auto player_system::toggle_weapon(
    ecs::entity player
) -> void {
    auto* state = world_->try_get<player_component>(player);
    if (state == nullptr || !state->hand_right_.is_valid()) {
        return;
    }

    const auto hand = state->hand_right_;
    auto& sockets   = world_->system<ecs::socket_system>();

    if (state->weapon_.is_valid()) {
        sockets.modify(hand).detach(std::string{weapon_socket});
        world_->destroy(state->weapon_);
        state->weapon_ = ecs::invalid_entity;
        return;
    }

    const auto weapon = world_->create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .with<ecs::model_component>()
        .get_entity();

    world_->system<ecs::model_system>().modify(weapon).set_model(
        assets_->get_model(weapon_prefab, weapon_node)
    );
    sockets.modify(hand).attach(std::string{weapon_socket}, weapon);
    world_->system<ecs::spatial_system>().modify(weapon).set_layer(ecs::spatial_layer::character);

    world_->get<player_component>(player).weapon_ = weapon;
}

auto player_system::update(
    float32
) -> void {
    auto& controllers = world_->system<ecs::character_controller_system>();
    auto& machines    = world_->system<ecs::animation_fsm_system>();

    toggling_.clear();

    world_->for_each<player_component, player_input_component>(
        [&](ecs::entity ent, player_component& state, const player_input_component& input) {
            if (world_->has<surface_placement_component>(ent)) {
                return;
            }

            const auto& frame = input.get_frame();
            const auto& body  = world_->get<ecs::rigid_body_component>(ent);

            const vec3f forward = frame.look_forward_flat();
            const vec3f right   = frame.look_right_flat();

            const vec3f move_dir =
                math::normalize(forward * frame.move_forward + right * frame.move_right);

            auto controller = controllers.modify(ent);
            controller.set_move_input(move_dir);

            const auto& layers = world_->get<ecs::animation_player_component>(ent);
            state.attacking_ =
                layers.has_layer(action_layer) && layers.get_layer(action_layer).is_active();

            if (state.attacking_) {
                controller.set_facing_direction(state.attack_facing_);
                controller.set_rotation_speed(attack_rotation_speed);
            } else {
                controller.set_rotation_speed(default_rotation_speed);
                if (math::length(move_dir) > math::epsilon) {
                    controller.set_facing_direction(move_dir);
                }
            }

            if (frame.was_pressed(input_action::attack) && state.weapon_.is_valid() &&
                !state.attacking_) {
                state.attack_facing_ = forward;
                machines.modify(ent).fire_trigger("attack");
            }

            const bool jump = frame.is_held(input_action::jump);
            if (jump) {
                controller.request_jump();
            }
            if (jump && body.is_grounded()) {
                state.jump_pending_ = true;
            }
            if (state.jump_pending_ && !body.is_grounded()) {
                state.jump_counter_ = (state.jump_counter_ + 1) % 2;
                state.jump_pending_ = false;
            }

            machines.modify(ent).set_parameter(
                "jump_count", static_cast<float32>(state.jump_counter_)
            );

            if (frame.was_pressed(input_action::toggle_weapon)) {
                toggling_.push_back(ent);
            }
        }
    );

    for (const ecs::entity ent : toggling_) {
        toggle_weapon(ent);
    }
}

}  // namespace vw::game
