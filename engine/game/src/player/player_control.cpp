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

constexpr std::size_t locomotion_layer = 0;
constexpr std::size_t action_layer     = 1;

auto rotated(const quat& q, const vec3f& v) -> vec3f {
    const vec3f axis{q.x, q.y, q.z};
    const vec3f t = math::cross(axis, v) * 2.0f;
    return v + t * q.w + math::cross(axis, t);
}

auto lean_degrees(float32 acceleration, float32 full_acceleration, float32 full_degrees)
    -> float32 {
    if (full_acceleration <= 0.0f) {
        return 0.0f;
    }
    return math::clamp(acceleration / full_acceleration, -1.0f, 1.0f) * full_degrees;
}

constexpr float32 longest_swing_seconds      = 2.0f;
constexpr float32 head_ignore_beyond_degrees = 110.0f;

auto age_buffer(float32& seconds_left, float32 delta_time) -> void {
    if (seconds_left < 0.0f) {
        return;
    }
    seconds_left -= delta_time;
    if (seconds_left < 0.0f) {
        seconds_left = -1.0f;
    }
}

auto lunge_speed(const movement_tuning& tuning, float32 seconds_into_lunge) -> float32 {
    if (tuning.lunge_seconds <= 0.0f) {
        return 0.0f;
    }
    const float32 progress = seconds_into_lunge / tuning.lunge_seconds;
    if (progress < 0.0f || progress >= 1.0f) {
        return 0.0f;
    }
    return 2.0f * tuning.lunge_distance / tuning.lunge_seconds * (1.0f - progress);
}

auto roll_progress(const movement_tuning& tuning, float32 seconds_into_roll) -> float32 {
    if (tuning.roll_seconds <= 0.0f) {
        return 1.0f;
    }
    const float32 u = math::clamp(seconds_into_roll / tuning.roll_seconds, 0.0f, 1.0f);
    return 1.5f * (u - u * u * u / 3.0f);
}

auto roll_turn_radians(const movement_tuning& tuning, float32 seconds_into_roll) -> float32 {
    const float32 full = 2.0f * std::numbers::pi_v<float32>;
    const float32 dive = math::radians(tuning.roll_dive_degrees);
    if (seconds_into_roll < tuning.roll_dive_seconds && tuning.roll_dive_seconds > 0.0f) {
        return dive * seconds_into_roll / tuning.roll_dive_seconds;
    }
    const float32 start = roll_progress(tuning, tuning.roll_dive_seconds);
    const float32 now   = roll_progress(tuning, seconds_into_roll);
    const float32 rest  = start < 1.0f ? (now - start) / (1.0f - start) : 1.0f;
    return dive + (full - dive) * math::clamp(rest, 0.0f, 1.0f);
}

auto roll_height_follow(const movement_tuning& tuning, bool rising, float32 delta_time) -> float32 {
    const float32 seconds = rising ? tuning.roll_height_rise_seconds : tuning.roll_height_fall_seconds;
    return seconds > 0.0f ? 1.0f - std::exp(-delta_time / seconds) : 1.0f;
}

auto roll_lift(const movement_tuning& tuning, float32 seconds_into_roll) -> float32 {
    const float32 span = tuning.roll_dive_seconds * 1.6f;
    if (span <= 0.0f || seconds_into_roll >= span) {
        return 0.0f;
    }
    return tuning.roll_dive_lift * std::sin(std::numbers::pi_v<float32> * seconds_into_roll / span);
}

auto dodge_travel_seconds(const movement_tuning& tuning, dodge_kind kind) -> float32 {
    return kind == dodge_kind::roll ? tuning.roll_seconds : tuning.dash_seconds;
}

auto dodge_total_seconds(const movement_tuning& tuning, dodge_kind kind) -> float32 {
    return kind == dodge_kind::roll ? tuning.roll_seconds + tuning.roll_recovery_seconds
                                    : tuning.dash_seconds + tuning.dash_recovery_seconds;
}

auto dodge_speed(const movement_tuning& tuning, dodge_kind kind, float32 seconds_into_dodge)
    -> float32 {
    const float32 seconds  = dodge_travel_seconds(tuning, kind);
    const float32 distance = kind == dodge_kind::roll ? tuning.roll_distance : tuning.dash_distance;
    if (seconds <= 0.0f) {
        return 0.0f;
    }
    const float32 u = seconds_into_dodge / seconds;
    if (u < 0.0f || u >= 1.0f) {
        return 0.0f;
    }
    return 1.5f * distance / seconds * (1.0f - u * u);
}

auto dodge_trigger(dodge_kind kind) -> std::string_view {
    return kind == dodge_kind::roll ? "dodge_roll" : "dodge_dash";
}

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
        .set_offset({0.0f, 13.5f, 0.0f});

    world_->system<ecs::spatial_system>().modify(root).set_layer(ecs::spatial_layer::character);

    const auto pose = world_->create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .get_entity();
    world_->system<ecs::hierarchy_system>().modify(pose).set_parent(root);

    player_component player;
    player.pose_       = pose;
    player.body_       = create_body_part_(pose, "body");
    player.head_       = create_body_part_(pose, "head");
    player.hand_right_ = create_body_part_(pose, "hand_right");
    player.hand_left_  = create_body_part_(pose, "hand_left");
    player.foot_right_ = create_body_part_(pose, "foot_right");
    player.foot_left_  = create_body_part_(pose, "foot_left");
    player.dodge_charges_ = tuning_.dodge_charges;

    attach_machines_(root);

    world_->modify(root).with<player_component>(std::move(player));
    return root;
}

auto player_system::create_body_part_(
    ecs::entity parent, std::string_view part_name
) const -> ecs::entity {
    const auto ent = world_->create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .get_entity();

    world_->system<ecs::hierarchy_system>().modify(ent).set_parent(parent);

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

auto player_system::lean_(
    ecs::entity ent, player_component& state, float32 delta_time
) const -> void {
    if (delta_time <= 0.0f || !state.pose_.is_valid()) {
        return;
    }

    const auto& wish = world_->get<ecs::movement_intent_component>(ent).get_wish_velocity();
    const vec3f planar_velocity{wish.x, 0.0f, wish.z};
    const vec3f acceleration = (planar_velocity - state.previous_planar_velocity_) / delta_time;
    state.previous_planar_velocity_ = planar_velocity;

    const auto facing      = world_->get<ecs::transform_component>(ent).get_rotation();
    const float32 forward  = math::dot(acceleration, rotated(facing, {0.0f, 0.0f, 1.0f}));
    const float32 sideways = math::dot(acceleration, rotated(facing, {1.0f, 0.0f, 0.0f}));

    const auto& controller = world_->get<ecs::character_controller_component>(ent);
    const float32 speed    = controller.get_move_speed();
    const float32 full_gain =
        tuning_.acceleration_seconds > 0.0f ? speed / tuning_.acceleration_seconds : 0.0f;
    const float32 full_loss =
        tuning_.deceleration_seconds > 0.0f ? speed / tuning_.deceleration_seconds : 0.0f;

    const float32 target_forward = forward >= 0.0f
        ? lean_degrees(forward, full_gain, tuning_.lean_forward_degrees)
        : lean_degrees(forward, full_loss, tuning_.lean_back_degrees);
    const float32 target_right = lean_degrees(sideways, full_gain, tuning_.lean_side_degrees);

    const float32 follow = tuning_.lean_follow_seconds > 0.0f
        ? 1.0f - std::exp(-delta_time / tuning_.lean_follow_seconds)
        : 1.0f;
    auto pose = world_->system<ecs::transform_system>().modify(state.pose_);

    if (state.is_rolling()) {
        state.lean_forward_degrees_ = 0.0f;
        state.lean_right_degrees_   = 0.0f;

        const float32 angle = roll_turn_radians(tuning_, state.dodge_elapsed_);
        const float32 half  = angle * 0.5f;
        const quat turn{std::sin(half), 0.0f, 0.0f, std::cos(half)};
        const vec3f pivot{0.0f, tuning_.roll_pivot_height, 0.0f};
        const vec3f swung = pivot - rotated(turn, pivot);
        const float32 rest_on_ground = -(swung.y + lowest_point_(state, turn));
        const float32 target = rest_on_ground + roll_lift(tuning_, state.dodge_elapsed_);
        state.roll_height_  += (target - state.roll_height_) *
            roll_height_follow(tuning_, target > state.roll_height_, delta_time);
        pose.set_rotation(turn);
        pose.set_position(swung + vec3f{0.0f, state.roll_height_, 0.0f});
        return;
    }

    state.roll_height_ -= state.roll_height_ * roll_height_follow(tuning_, state.roll_height_ < 0.0f, delta_time);

    state.lean_forward_degrees_ += (target_forward - state.lean_forward_degrees_) * follow;
    state.lean_right_degrees_ += (target_right - state.lean_right_degrees_) * follow;

    pose.set_rotation(math::euler_to_quat(
        {math::radians(state.lean_forward_degrees_), 0.0f, -math::radians(state.lean_right_degrees_)}
    ));
    pose.set_position({0.0f, state.roll_height_, 0.0f});
}

auto player_system::lowest_point_(
    const player_component& state, const quat& turn
) const -> float32 {
    float32 lowest = std::numeric_limits<float32>::max();
    for (const ecs::entity part : {state.body_, state.head_, state.hand_right_, state.hand_left_,
                                   state.foot_right_, state.foot_left_}) {
        if (!part.is_valid() || !world_->has<ecs::model_component>(part)) {
            continue;
        }
        const auto& model = world_->get<ecs::model_component>(part).get_model();
        if (!model) {
            continue;
        }

        const auto& placed  = world_->get<ecs::transform_component>(part);
        const float32 unit  = static_cast<float32>(model->world_units_per_voxel());
        const vec3i size    = model->size();
        const vec3f low     = model->pivot() * -unit;
        const vec3f high    = vec3f{static_cast<float32>(size.x), static_cast<float32>(size.y),
                                    static_cast<float32>(size.z)} * unit + low;
        const vec3f& scale  = placed.get_scale();

        for (const float32 x : {low.x, high.x}) {
            for (const float32 y : {low.y, high.y}) {
                for (const float32 z : {low.z, high.z}) {
                    const vec3f corner{x * scale.x, y * scale.y, z * scale.z};
                    const vec3f in_pose = placed.get_position() + rotated(placed.get_rotation(), corner);
                    lowest = std::min(lowest, rotated(turn, in_pose).y);
                }
            }
        }
    }
    return lowest == std::numeric_limits<float32>::max() ? 0.0f : lowest;
}

auto player_system::turn_head_(
    ecs::entity ent, player_component& state, const vec3f& look, float32 delta_time
) const -> void {
    if (!state.head_.is_valid()) {
        return;
    }

    const auto facing   = world_->get<ecs::transform_component>(ent).get_rotation();
    const vec3f forward = rotated(facing, {0.0f, 0.0f, 1.0f});
    const vec3f body{forward.x, 0.0f, forward.z};

    float32 target = 0.0f;
    if (math::length(body) > math::epsilon && math::length(look) > math::epsilon) {
        const vec3f along   = math::normalize(body);
        const vec3f towards = math::normalize(vec3f{look.x, 0.0f, look.z});
        const float32 apart = math::degrees(
            std::atan2(math::cross(along, towards).y, math::dot(along, towards))
        );
        if (std::abs(apart) <= head_ignore_beyond_degrees) {
            target = math::clamp(apart, -tuning_.head_turn_degrees, tuning_.head_turn_degrees);
        }
    }

    const float32 follow = tuning_.head_follow_seconds > 0.0f
        ? 1.0f - std::exp(-delta_time / tuning_.head_follow_seconds)
        : 1.0f;
    state.head_yaw_degrees_ += (target - state.head_yaw_degrees_) * follow;

    const float32 half = math::radians(state.head_yaw_degrees_) * 0.5f;
    world_->system<ecs::animation_system>().modify_adjustment(state.head_).set_rotation(
        quat{0.0f, std::sin(half), 0.0f, std::cos(half)}
    );
}

auto player_system::swing_legs_(
    ecs::entity ent, player_component& state, float32 delta_time
) const -> void {
    float32 target = 0.0f;
    if (state.air_state_ != air_state::ground) {
        const auto& body      = world_->get<ecs::rigid_body_component>(ent);
        const float32 speed   = world_->get<ecs::character_controller_component>(ent).get_move_speed();
        const vec3f velocity  = body.get_velocity();
        const float32 planar  = math::length(vec3f{velocity.x, 0.0f, velocity.z});
        const float32 reach   = speed > 0.0f ? math::clamp(planar / speed, 0.0f, 1.0f) : 0.0f;
        const float32 leading = state.pushed_with_left_ ? 1.0f : -1.0f;
        target                = leading * reach;
    }

    const float32 follow = tuning_.stride_follow_seconds > 0.0f
        ? 1.0f - std::exp(-delta_time / tuning_.stride_follow_seconds)
        : 1.0f;
    state.stride_ += (target - state.stride_) * follow;

    const auto pitch = [this](float32 lead) {
        const float32 degrees = lead >= 0.0f ? -lead * tuning_.stride_lead_pitch_degrees
                                             : -lead * tuning_.stride_trail_pitch_degrees;
        const float32 half = math::radians(degrees) * 0.5f;
        return quat{std::sin(half), 0.0f, 0.0f, std::cos(half)};
    };

    auto& animation = world_->system<ecs::animation_system>();
    const float32 step = state.stride_ * tuning_.stride_voxels;
    const float32 arm  = state.stride_ * tuning_.stride_arm_swing_voxels;
    if (state.foot_right_.is_valid()) {
        animation.modify_adjustment(state.foot_right_)
            .set_translation({0.0f, 0.0f, step})
            .set_rotation(pitch(state.stride_));
    }
    if (state.foot_left_.is_valid()) {
        animation.modify_adjustment(state.foot_left_)
            .set_translation({0.0f, 0.0f, -step})
            .set_rotation(pitch(-state.stride_));
    }
    if (state.hand_right_.is_valid()) {
        animation.modify_adjustment(state.hand_right_).set_translation({0.0f, 0.0f, -arm});
    }
    if (state.hand_left_.is_valid()) {
        animation.modify_adjustment(state.hand_left_).set_translation({0.0f, 0.0f, arm});
    }
}

auto player_system::read_action_events_(
    player_component& state, const ecs::animation_player_component& layers
) -> void {
    for (const auto& event : layers.get_fired_events()) {
        if (event.layer == locomotion_layer) {
            if (event.name == "footstep") {
                state.pushed_with_left_ = event.payload == "left";
            } else if (event.name == "control.lock") {
                state.body_locked_         = true;
                state.body_locked_seconds_ = 0.0f;
            } else if (event.name == "control.unlock") {
                state.body_locked_ = false;
            } else if (event.name == "iframe.start" && state.dodging_) {
                state.invulnerable_ = true;
            } else if (event.name == "iframe.end") {
                state.invulnerable_ = false;
            }
            continue;
        }
        if (event.layer != action_layer) {
            continue;
        }

        if (event.name == "control.lock") {
            state.swing_started_ = true;
        } else if (event.name == "control.unlock") {
            end_swing_(state);
        } else if (event.name == "move.start" && state.swinging_) {
            state.lunging_       = true;
            state.lunged_        = true;
            state.lunge_seconds_ = 0.0f;
        } else if (event.name == "move.end") {
            state.lunging_ = false;
        } else if (event.name == "hit.start" && state.swinging_) {
            state.hit_window_ = true;
        } else if (event.name == "hit.end") {
            state.hit_window_ = false;
        } else if (event.name == "cancel.ok" && state.swinging_) {
            state.cancel_open_ = true;
        }
    }
}

auto player_system::recharge_dodge_(
    player_component& state, float32 delta_time
) const -> void {
    state.dodge_charges_ = std::min(state.dodge_charges_, tuning_.dodge_charges);
    if (state.dodging_ || state.dodge_charges_ >= tuning_.dodge_charges) {
        state.dodge_recharge_left_ = 0.0f;
        return;
    }
    if (state.dodge_recharge_left_ <= 0.0f) {
        state.dodge_recharge_left_ = tuning_.dodge_recharge_seconds;
    }
    state.dodge_recharge_left_ -= delta_time;
    if (state.dodge_recharge_left_ <= 0.0f) {
        state.dodge_recharge_left_ = 0.0f;
        ++state.dodge_charges_;
    }
}

auto player_system::end_swing_(
    player_component& state
) -> void {
    state.swinging_    = false;
    state.lunging_     = false;
    state.hit_window_  = false;
    state.cancel_open_ = false;
}

auto player_system::update(
    float32 delta_time
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

            const vec3f forward = frame.look_forward_flat();
            const vec3f right   = frame.look_right_flat();

            const vec3f move_dir =
                math::normalize(forward * frame.move_forward + right * frame.move_right);

            const bool moving = math::length(move_dir) > math::epsilon;

            const auto& layers = world_->get<ecs::animation_player_component>(ent);
            const bool action_playing =
                layers.has_layer(action_layer) && layers.get_layer(action_layer).is_active();

            read_action_events_(state, layers);
            if (state.swinging_) {
                state.swing_started_ = state.swing_started_ || action_playing;
                const bool clip_gone = state.swing_started_ && !action_playing;
                if (clip_gone || state.swing_seconds_ > longest_swing_seconds) {
                    end_swing_(state);
                }
            }
            if (state.body_locked_) {
                state.body_locked_seconds_ += delta_time;
                state.body_locked_ = state.body_locked_seconds_ <= longest_swing_seconds;
            }

            if (frame.was_pressed(input_action::attack)) {
                state.attack_buffered_ = tuning_.input_buffer_seconds;
            }
            if (frame.was_pressed(input_action::jump)) {
                state.jump_buffered_ = tuning_.input_buffer_seconds;
            }
            if (frame.was_pressed(input_action::dodge)) {
                state.dodge_buffered_ = tuning_.input_buffer_seconds;
            }

            const bool dodge_allowed = !state.dodging_ && !state.body_locked_ &&
                !state.hit_window_ && state.air_state_ == air_state::ground &&
                state.dodge_charges_ > 0;
            if (state.dodge_buffered_ >= 0.0f && dodge_allowed) {
                state.dodge_buffered_ = -1.0f;
                --state.dodge_charges_;
                ++state.dodge_count_;
                state.invulnerable_ = false;
                const auto facing = world_->get<ecs::transform_component>(ent).get_rotation();
                const vec3f look  = rotated(facing, {0.0f, 0.0f, 1.0f});
                state.dodge_facing_ = moving ? move_dir : math::normalize(vec3f{look.x, 0.0f, look.z});
                state.dodge_kind_    = tuning_.dodge;
                state.dodging_       = true;
                state.dodge_elapsed_ = 0.0f;
                state.roll_height_   = 0.0f;
                state.attack_buffered_ = -1.0f;
                if (state.swinging_) {
                    end_swing_(state);
                }
                machines.modify(ent).fire_trigger(dodge_trigger(state.dodge_kind_));
                machines.modify(ent).fire_trigger("dodge");
            }

            const bool strike_allowed = !state.dodging_ && !state.body_locked_ &&
                (!state.swinging_ || state.cancel_open_);
            if (state.attack_buffered_ >= 0.0f && state.weapon_.is_valid() && strike_allowed) {
                state.attack_buffered_ = -1.0f;
                state.cancel_open_     = false;
                ++state.swing_count_;
                const auto facing = world_->get<ecs::transform_component>(ent).get_rotation();
                const vec3f look  = rotated(facing, {0.0f, 0.0f, 1.0f});
                state.attack_facing_ = moving ? move_dir : math::normalize(vec3f{look.x, 0.0f, look.z});
                state.swinging_      = true;
                state.swing_started_ = false;
                state.swing_seconds_ = 0.0f;
                state.lunging_       = false;
                state.lunged_        = false;
                state.hit_window_    = false;
                machines.modify(ent).fire_trigger("attack");
            }
            state.attacking_ = action_playing || state.swinging_;

            auto controller = controllers.modify(ent);
            if (state.dodging_) {
                const float32 move_speed =
                    world_->get<ecs::character_controller_component>(ent).get_move_speed();
                const float32 dodge = dodge_speed(
                    tuning_, state.dodge_kind_, state.dodge_elapsed_ + delta_time * 0.5f
                );
                controller.set_move_input(state.dodge_facing_ * (dodge / move_speed))
                    .set_acceleration_seconds(0.0f)
                    .set_deceleration_seconds(0.0f)
                    .set_facing_direction(state.dodge_facing_)
                    .set_turn_degrees_per_second(tuning_.attack_turn_degrees_per_second);

                state.dodge_elapsed_ += delta_time;
                state.dodging_ =
                    state.dodge_elapsed_ < dodge_total_seconds(tuning_, state.dodge_kind_);
                state.invulnerable_ = state.invulnerable_ && state.dodging_;
            } else if (state.swinging_) {
                const float32 lunge = state.lunging_
                    ? lunge_speed(tuning_, state.lunge_seconds_ + delta_time * 0.5f)
                    : 0.0f;
                const float32 move_speed =
                    world_->get<ecs::character_controller_component>(ent).get_move_speed();
                controller.set_move_input(state.attack_facing_ * (lunge / move_speed))
                    .set_acceleration_seconds(0.0f)
                    .set_deceleration_seconds(state.lunged_ ? 0.0f : tuning_.deceleration_seconds)
                    .set_facing_direction(state.attack_facing_)
                    .set_turn_degrees_per_second(tuning_.attack_turn_degrees_per_second);

                state.swing_seconds_ += delta_time;
                if (state.lunging_) {
                    state.lunge_seconds_ += delta_time;
                    state.lunging_ = state.lunge_seconds_ < tuning_.lunge_seconds;
                }
            } else {
                const bool steering = moving && !state.body_locked_;
                controller.set_move_input(steering ? move_dir : vec3f{0.0f, 0.0f, 0.0f})
                    .set_acceleration_seconds(tuning_.acceleration_seconds)
                    .set_deceleration_seconds(tuning_.deceleration_seconds)
                    .set_turn_degrees_per_second(tuning_.run_turn_degrees_per_second);
                if (steering) {
                    controller.set_facing_direction(move_dir);
                }
            }

            recharge_dodge_(state, delta_time);

            const uint32 jump_count =
                world_->get<ecs::character_controller_component>(ent).get_jump_count();
            if (jump_count != state.seen_jump_count_) {
                state.seen_jump_count_ = jump_count;
                state.jump_buffered_   = -1.0f;
            }

            controller.set_coyote_seconds(tuning_.coyote_seconds);
            if (state.jump_buffered_ >= 0.0f && !state.body_locked_ && !state.dodging_) {
                controller.request_jump();
            }

            const auto& body        = world_->get<ecs::rigid_body_component>(ent);
            const auto& cc          = world_->get<ecs::character_controller_component>(ent);
            const air_state before  = state.air_state_;
            if (body.is_grounded()) {
                state.air_state_ = air_state::ground;
            } else if (cc.left_ground_by_jump() && body.get_velocity().y > 0.0f) {
                state.air_state_ = air_state::rising;
            } else if (cc.left_ground_by_jump() ||
                       cc.get_seconds_off_ground() > tuning_.fall_after_seconds) {
                state.air_state_ = air_state::falling;
            }
            machines.modify(ent).set_parameter(
                "air_state", static_cast<float32>(std::to_underlying(state.air_state_))
            );

            if (state.air_state_ != air_state::ground) {
                state.fall_speed_ = std::max(state.fall_speed_, -body.get_velocity().y);
            } else if (before != air_state::ground) {
                const bool hard   = state.fall_speed_ >= tuning_.hard_landing_speed;
                state.fall_speed_ = 0.0f;
                if (hard) {
                    ++state.hard_landings_;
                    machines.modify(ent).fire_trigger("land_hard");
                } else {
                    ++state.soft_landings_;
                    machines.modify(ent).fire_trigger("land");
                }
            }

            age_buffer(state.attack_buffered_, delta_time);
            age_buffer(state.jump_buffered_, delta_time);
            age_buffer(state.dodge_buffered_, delta_time);

            if (frame.was_pressed(input_action::toggle_weapon)) {
                toggling_.push_back(ent);
            }

            lean_(ent, state, delta_time);
            turn_head_(ent, state, forward, delta_time);
            swing_legs_(ent, state, delta_time);
        }
    );

    for (const ecs::entity ent : toggling_) {
        toggle_weapon(ent);
    }
}

}  // namespace vw::game
