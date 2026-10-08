module vw.game;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

namespace vw::game {
namespace {

constexpr ecs::spatial_layer_mask struck_layers =
    ecs::spatial_layer::terrain | ecs::spatial_layer::character | ecs::spatial_layer::prop;

auto turned(const quat& q, const vec3f& v) -> vec3f {
    const vec3f axis{q.x, q.y, q.z};
    const vec3f t = math::cross(axis, v) * 2.0f;
    return v + t * q.w + math::cross(axis, t);
}

auto reversed(const quat& q) -> quat {
    return {-q.x, -q.y, -q.z, q.w};
}

auto facing_along(const vec3f& direction) -> quat {
    const vec3f ahead{0.0f, 0.0f, 1.0f};
    const float32 length = math::length(direction);
    if (length <= math::epsilon) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const vec3f towards  = direction / length;
    const float32 cosine = math::dot(ahead, towards);
    if (cosine < -1.0f + 1.0e-5f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }
    const vec3f axis = math::cross(ahead, towards);
    return math::normalize(quat{axis.x, axis.y, axis.z, 1.0f + cosine});
}

auto tip_reach(const ecs::model_component& shape) -> float32 {
    const auto& model = shape.get_model();
    if (!model) {
        return 0.0f;
    }
    const float32 unit = static_cast<float32>(model->world_units_per_voxel());
    return (static_cast<float32>(model->size().z) - model->pivot().z) * unit;
}

}  // namespace

projectile_system::projectile_system(
    ecs::world& w
)
    : world_{&w} {}

auto projectile_system::root_of_(
    ecs::entity ent
) const -> ecs::entity {
    ecs::entity top = ent;
    while (true) {
        const auto* links = world_->try_get<ecs::hierarchy_component>(top);
        if (links == nullptr || !links->has_parent()) {
            return top;
        }
        top = links->get_parent();
    }
}

auto projectile_system::first_hit(
    const vec3f& from, const vec3f& to, ecs::entity ignored_owner
) -> std::optional<projectile_hit> {
    if (math::distance_squared(from, to) <= math::epsilon) {
        return std::nullopt;
    }

    const auto& tree = world_->system<ecs::spatial_system>();
    const spatial::ray path{from, to};

    candidates_.clear();
    tree.query_all(path, candidates_, struck_layers);
    if (ignored_owner.is_valid()) {
        std::erase_if(candidates_, [&](ecs::entity ent) { return root_of_(ent) == ignored_owner; });
    }

    const auto struck = tree.closest_voxel_hit(path, candidates_);
    if (!struck) {
        return std::nullopt;
    }

    const auto& placed = world_->get<ecs::transform_component>(struck->ent);
    const auto& shape  = world_->get<ecs::model_component>(struck->ent);
    const mat4f placing = ecs::model_matrix(placed, shape);
    const mat4f back    = math::inverse_matrix(placing).value_or(math::identity_matrix());

    const vec3f corner{
        static_cast<float32>(struck->voxel_pos.x), static_cast<float32>(struck->voxel_pos.y),
        static_cast<float32>(struck->voxel_pos.z)
    };
    const spatial::aabb cell{.min = corner, .max = corner + vec3f{1.0f, 1.0f, 1.0f}};
    const spatial::ray inside{back * from, back * to};

    // см. docs/ENGINE.md#снаряды
    float32 entered  = 0.0f;
    const vec3f met  = inside.intersects_at(cell, entered) ? inside.point_at(entered) : cell.center();
    const float32 length = path.length();
    const float32 reach  = math::dot(placing * met - from, path.direction);
    if (reach > length) {
        return std::nullopt;
    }
    const float32 along = math::clamp(reach, 0.0f, length);

    const auto* bounds = world_->try_get<ecs::spatial_component>(struck->ent);
    const bool ground  = bounds != nullptr && (bounds->get_layer() & ecs::spatial_layer::terrain) != 0;

    return projectile_hit{
        .projectile = ecs::invalid_entity,
        .owner      = ignored_owner,
        .target     = ground ? ecs::invalid_entity : root_of_(struck->ent),
        .part       = struck->ent,
        .point      = from + path.direction * along,
        .velocity   = {0.0f, 0.0f, 0.0f},
    };
}

auto projectile_system::launch(
    const projectile_launch& shot
) -> ecs::entity {
    const auto ent = world_->create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>()
        .with<ecs::model_component>()
        .get_entity();

    world_->system<ecs::model_system>().modify(ent).set_model(shot.model);
    world_->system<ecs::spatial_system>().modify(ent).set_layer(ecs::spatial_layer::projectile);
    world_->system<ecs::transform_system>()
        .modify(ent)
        .set_position(shot.position)
        .set_rotation(facing_along(shot.velocity));

    projectile_component flying;
    flying.owner_    = shot.owner;
    flying.velocity_ = shot.velocity;
    world_->modify(ent).with<projectile_component>(std::move(flying));

    ++launched_count_;
    return ent;
}

auto projectile_system::fly_(
    ecs::entity ent, projectile_component& shot, float32 delta_time
) -> void {
    shot.age_seconds_ += delta_time;
    if (shot.age_seconds_ > tuning_.flight_seconds) {
        drop_(ent);
        return;
    }
    ++flying_count_;

    const float32 gravity =
        world_->system<ecs::physics_system>().get_gravity() * tuning_.gravity_scale;
    shot.velocity_.y += gravity * delta_time;

    const float32 speed = math::length(shot.velocity_);
    if (speed <= math::epsilon) {
        return;
    }
    const vec3f heading = shot.velocity_ / speed;
    const float32 tip   = tip_reach(world_->get<ecs::model_component>(ent));

    const vec3f from = world_->get<ecs::transform_component>(ent).get_position();
    const vec3f to   = from + shot.velocity_ * delta_time;

    auto hit = first_hit(from, to + heading * tip, shot.owner_);
    if (hit) {
        hit->projectile = ent;
        hit->velocity   = shot.velocity_;
        world_->system<ecs::transform_system>()
            .modify(ent)
            .set_position(hit->point - heading * (tip - tuning_.sink_units))
            .set_rotation(facing_along(heading));
        stick_(ent, shot, *hit);
        return;
    }

    world_->system<ecs::transform_system>()
        .modify(ent)
        .set_position(to)
        .set_rotation(facing_along(heading));
}

auto projectile_system::stick_(
    ecs::entity ent, projectile_component& shot, const projectile_hit& hit
) -> void {
    shot.stuck_       = true;
    shot.stuck_in_    = hit.target;
    shot.velocity_    = {0.0f, 0.0f, 0.0f};
    shot.age_seconds_ = 0.0f;

    if (hit.target.is_valid()) {
        const auto& carrier = world_->get<ecs::transform_component>(hit.target);
        const auto& own     = world_->get<ecs::transform_component>(ent);
        const quat back     = reversed(carrier.get_rotation());
        shot.held_at_       = turned(back, own.get_position() - carrier.get_position());
        shot.held_turn_     = back * own.get_rotation();
        ++entity_hit_count_;
    }

    hits_.push_back(hit);
    stuck_.push_back(ent);
    while (stuck_.size() > tuning_.stuck_limit) {
        dropped_.push_back(stuck_.front());
        stuck_.pop_front();
    }
}

auto projectile_system::hold_(
    ecs::entity ent, projectile_component& shot, float32 delta_time
) -> void {
    shot.age_seconds_ += delta_time;
    if (shot.age_seconds_ > tuning_.stuck_seconds) {
        drop_(ent);
        return;
    }
    if (!shot.stuck_in_.is_valid()) {
        return;
    }

    const auto* carrier = world_->registry().alive(shot.stuck_in_)
        ? world_->try_get<ecs::transform_component>(shot.stuck_in_)
        : nullptr;
    if (carrier == nullptr) {
        drop_(ent);
        return;
    }

    world_->system<ecs::transform_system>()
        .modify(ent)
        .set_position(carrier->get_position() + turned(carrier->get_rotation(), shot.held_at_))
        .set_rotation(carrier->get_rotation() * shot.held_turn_);
}

auto projectile_system::drop_(
    ecs::entity ent
) -> void {
    dropped_.push_back(ent);
    std::erase(stuck_, ent);
}

auto projectile_system::update(
    float32 delta_time
) -> void {
    hits_.clear();
    flying_count_ = 0;

    world_->for_each<projectile_component>([&](ecs::entity ent, projectile_component& shot) {
        if (std::ranges::contains(dropped_, ent)) {
            return;
        }
        if (shot.stuck_) {
            hold_(ent, shot, delta_time);
        } else {
            fly_(ent, shot, delta_time);
        }
    });

    for (const ecs::entity ent : dropped_) {
        if (world_->registry().alive(ent)) {
            world_->destroy(ent);
        }
    }
    dropped_.clear();
}

}  // namespace vw::game
