module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

template <typename F>
auto measure_into(bool enabled, float32& sink, F&& body) -> void {
    if (!enabled) {
        body();
        return;
    }

    using clock = std::chrono::high_resolution_clock;
    const auto started = clock::now();
    body();
    sink += std::chrono::duration<float32>(clock::now() - started).count() * 1000.0F;
}

class solid_in_grid final {
public:
    explicit solid_in_grid(world_grid& grid) : grid_{&grid}, units_{grid.world_units_per_voxel()} {}

    [[nodiscard]] auto operator()(int32 vx, int32 vy, int32 vz) const -> bool {
        const vec3i at{vx * units_, vy * units_, vz * units_};

        const auto coord = grid_->world_to_chunk_coord(at);
        if (coord != cached_coord_) {
            cached_coord_ = coord;
            cached_chunk_ = grid_->get_chunk(coord);
        }
        return cached_chunk_ != nullptr &&
               !cached_chunk_->get_voxel(grid_->world_to_local_coord(at) / units_).is_empty();
    }

private:
    world_grid* grid_;
    int32 units_;
    mutable vec3i cached_coord_{std::numeric_limits<int32>::min(), 0, 0};
    mutable chunk* cached_chunk_ = nullptr;
};

}  // namespace

physics_system::physics_system(world& w)
    : world_(&w) {}

auto physics_system::set_gravity(float32 g) -> void {
    gravity_ = g;
}

auto physics_system::get_gravity() const -> float32 {
    return gravity_;
}

auto physics_system::update(
    float32 delta_time
) -> void {
    if (!world_->system<world_grid_system>().grid()) {
        return;
    }

    stats_           = {};
    detailed_active_ = std::exchange(detailed_requested_, false);

    accumulated_time_ += delta_time;
    auto max_accumulated = fixed_dt * static_cast<float32>(max_steps_per_frame);
    if (accumulated_time_ > max_accumulated) {
        accumulated_time_ = max_accumulated;
    }

    using clock = std::chrono::high_resolution_clock;
    const auto step_start = clock::now();

    while (accumulated_time_ >= fixed_dt) {
        step(fixed_dt);
        accumulated_time_ -= fixed_dt;
        ++stats_.step_count;
    }

    show_between_steps_(delta_time);

    stats_.step_ms = std::chrono::duration<float32>(clock::now() - step_start).count() * 1000.0f;
}

auto physics_system::get_stats() const -> const physics_stats& {
    return stats_;
}

auto physics_system::request_detailed_stats() -> void {
    detailed_requested_ = true;
}

auto physics_system::step(
    float32 dt
) -> void {
    constexpr float32 impulse_epsilon = 0.01f;

    auto& reg = world_->registry();
    for (auto [ent, rb, tc] :
         reg.view<rigid_body_component, transform_component>()) {
        auto position = tc.get_position();

        rb.stepped_from_ = position;
        rb.stepped_to_   = position;

        rb.velocity_.y += gravity_ * rb.gravity_scale_ * dt;

        if (auto* mi_ptr = reg.try_get<movement_intent_component>(ent)) {
            auto& mi = *mi_ptr;
            auto axes = mi.wish_axes_;

            if (axes & axis_flag::x) { rb.velocity_.x = mi.wish_velocity_.x; }
            if (axes & axis_flag::y) {
                rb.velocity_.y = mi.wish_velocity_.y;
                mi.wish_axes_  = static_cast<axis_flags>(axes & ~axis_flag::y);
            }
            if (axes & axis_flag::z) { rb.velocity_.z = mi.wish_velocity_.z; }
        }

        rb.velocity_ = rb.velocity_ + rb.impulse_;

        auto decay = 1.0f - rb.drag_ * dt;
        if (decay < 0.0f) {
            decay = 0.0f;
        }
        rb.impulse_ = rb.impulse_ * decay;
        if (math::dot(rb.impulse_, rb.impulse_) < impulse_epsilon * impulse_epsilon) {
            rb.impulse_ = {0.0f, 0.0f, 0.0f};
        }

        auto new_position = position + rb.velocity_ * dt;

        if (const auto* col_ptr = reg.try_get<box_collider_component>(ent)) {
            const auto& col = *col_ptr;
            auto half = col.extents_ * 0.5f;

            if (!are_chunks_loaded(new_position + col.offset_, col.extents_)) {
                rb.frozen_ = true;
                rb.velocity_ = {0.0f, 0.0f, 0.0f};
                rb.impulse_ = {0.0f, 0.0f, 0.0f};
                continue;
            }

            rb.frozen_     = false;
            rb.ride_probe_ = {};

            const auto voxel_size =
                static_cast<float32>(world_->system<world_grid_system>().grid()->world_units_per_voxel());
            if (const auto* rider = reg.try_get<character_controller_component>(ent);
                rider != nullptr && rider->get_leg_voxels() > 0.0f) {
                new_position   = ride_footing_(ent, rb, col, *rider, position, dt);
                rb.stepped_to_ = new_position;
                world_->system<transform_system>().modify(ent).set_position(new_position);
                continue;
            }
            const auto substeps = std::max(
                1, static_cast<int32>(std::ceil(math::length(rb.velocity_ * dt) / (voxel_size * max_substep_voxels)))
            );
            const float32 sub_dt = dt / static_cast<float32>(substeps);

            new_position = position;
            rb.grounded_ = false;
            for (int32 sub = 0; sub < substeps; ++sub) {
                new_position = new_position + (rb.velocity_ * sub_dt);

                collision_result result{};
                measure_into(detailed_active_, stats_.voxel_collision_ms, [&] {
                    result = resolve_box_voxel(new_position + col.offset_, half, rb.velocity_);
                });

                new_position = result.resolved_position - col.offset_;
                rb.grounded_ = rb.grounded_ || result.grounded;

                measure_into(detailed_active_, stats_.entity_collision_ms, [&] {
                    resolve_entity_collisions(ent, new_position, rb.velocity_, half, col.offset_);
                });
            }

        }

        rb.stepped_to_ = new_position;
        world_->system<transform_system>().modify(ent).set_position(new_position);
    }
}

// см. docs/ENGINE.md#езда-по-опоре
auto physics_system::ride_footing_(
    entity ent, rigid_body_component& rb, const box_collider_component& col,
    const character_controller_component& walker, const vec3f& from, float32 dt
) -> vec3f {
    auto& grid               = *world_->system<world_grid_system>().grid();
    const float32 voxel_size = static_cast<float32>(grid.world_units_per_voxel());
    const solid_in_grid solid{grid};

    const vec3f half         = col.extents_ * 0.5f;
    const float32 height     = col.extents_.y;
    const float32 leg        = std::min(walker.get_leg_voxels() * voxel_size, height * longest_leg_share);
    const float32 feet_below = half.y - col.offset_.y;
    const bool may_climb     = walker.get_step_hop_voxels() > 0.0f;
    const float32 rise       = walker.get_ride_rise_speed();
    const float32 sink       = walker.get_ride_sink_speed();

    const vec3f wish{rb.velocity_.x, 0.0f, rb.velocity_.z};
    const float32 wish_speed = math::length(wish);
    const float32 pace       = walker.get_move_speed() > 0.0f
        ? std::clamp(wish_speed / walker.get_move_speed(), 0.0f, 1.0f)
        : 0.0f;
    const float32 lead       = walker.get_ride_lead_voxels() * voxel_size * pace;
    const vec3f heading      = wish_speed > 1.0f ? wish * (1.0f / wish_speed) : vec3f{0.0f, 0.0f, 0.0f};

    const auto under = [&](float32 x, float32 z) {
        return footprint{
            .x = x + col.offset_.x, .z = z + col.offset_.z, .half_x = half.x, .half_z = half.z
        };
    };

    struct ground {
        float32 stand = 0.0f;
        float32 ride  = 0.0f;
        bool found    = false;
    };
    const auto ground_at = [&](float32 x, float32 z, float32 feet) -> ground {
        const auto here = footing_under(solid, voxel_size, under(x, z), feet, leg);
        if (!here.found) {
            return {};
        }

        float32 ride = here.stand;
        if (lead <= ride_look_past) {
            return {.stand = here.stand, .ride = ride, .found = true};
        }
        for (const float32 way : {1.0f, -1.0f}) {
            if (way > 0.0f && !may_climb) {
                continue;
            }
            const float32 along_x = heading.x * way;
            const float32 along_z = heading.z * way;

            std::array<float32, ride_crossings> crossings{};
            std::size_t count = 0;
            const auto collect = [&](float32 centre, float32 half_width, float32 along) {
                if (std::abs(along) < 1.0e-4f) {
                    return;
                }
                const float32 edge = centre + (along > 0.0f ? half_width : -half_width);
                float32 wall       = along > 0.0f
                    ? std::ceil((edge - ride_look_past) / voxel_size) * voxel_size
                    : std::floor((edge + ride_look_past) / voxel_size) * voxel_size;
                for (; count < crossings.size(); wall += along > 0.0f ? voxel_size : -voxel_size) {
                    const float32 reached = std::max((wall - edge) / along, 0.0f);
                    if (reached > lead) {
                        break;
                    }
                    crossings[count++] = reached;
                }
            };
            collect(x + col.offset_.x, half.x, along_x);
            collect(z + col.offset_.z, half.z, along_z);

            for (std::size_t at = 0; at < count; ++at) {
                const float32 gone = crossings[at] + ride_look_past;
                const auto spot    = under(x + (along_x * gone), z + (along_z * gone));
                const auto there   = footing_under(solid, voxel_size, spot, feet, leg);
                if (!there.found || there.stand > here.stand + leg + footing_reach_slack) {
                    continue;
                }
                const float32 lifted = there.stand - (leg * crossings[at] / lead);
                if (lifted > ride && (way < 0.0f || body_fits(solid, voxel_size, spot, there.stand + leg,
                                                             there.stand + height))) {
                    ride = lifted;
                }
            }
        }
        return {.stand = here.stand, .ride = ride, .found = true};
    };

    float32 x    = from.x;
    float32 z    = from.z;
    float32 feet = from.y - feet_below;
    bool riding  = rb.grounded_ && rb.velocity_.y <= 0.0f;

    const auto substeps = std::max(
        1, static_cast<int32>(std::ceil(math::length(rb.velocity_ * dt) / (voxel_size * max_substep_voxels)))
    );
    const float32 sub_dt = dt / static_cast<float32>(substeps);
    const auto towards   = [&](float32 now, float32 goal) {
        return now + std::clamp(goal - now, -sink * sub_dt, rise * sub_dt);
    };

    for (int32 sub = 0; sub < substeps; ++sub) {
        if (riding) {
            const float32 wanted_x = rb.velocity_.x * sub_dt;
            const float32 wanted_z = rb.velocity_.z * sub_dt;
            const std::array<std::pair<float32, float32>, 3> tries{
                std::pair{wanted_x, wanted_z}, std::pair{wanted_x, 0.0f}, std::pair{0.0f, wanted_z}
            };

            bool moved = false;
            for (const auto& [move_x, move_z] : tries) {
                if (moved || (move_x == 0.0f && move_z == 0.0f)) {
                    continue;
                }
                const auto there = ground_at(x + move_x, z + move_z, feet);
                const bool off   = !there.found || there.ride < feet - leg - ride_drop_slack;
                const float32 stood = off ? feet : towards(feet, there.ride);

                if (!off && there.stand > stood + ride_climb_slack) {
                    continue;
                }
                if (!off && !may_climb && there.stand > feet + ride_climb_slack) {
                    continue;
                }
                if (!body_fits(
                        solid, voxel_size, under(x + move_x, z + move_z), stood + leg, stood + height
                    )) {
                    continue;
                }

                x += move_x;
                z += move_z;
                feet = stood;
                if (move_x == 0.0f) {
                    rb.velocity_.x = 0.0f;
                }
                if (move_z == 0.0f) {
                    rb.velocity_.z = 0.0f;
                }
                riding = !off;
                moved  = true;
            }

            if (!moved) {
                rb.velocity_.x = 0.0f;
                rb.velocity_.z = 0.0f;

                const auto here = ground_at(x, z, feet);
                if (!here.found || here.ride < feet - leg - ride_drop_slack) {
                    riding = false;
                } else {
                    const float32 stood = towards(feet, here.ride);
                    if (stood <= feet ||
                        body_fits(solid, voxel_size, under(x, z), stood + leg, stood + height)) {
                        feet = stood;
                    }
                }
            }
            rb.velocity_.y = 0.0f;
        } else {
            const vec3f centre{
                x + col.offset_.x + (rb.velocity_.x * sub_dt),
                feet + half.y + (rb.velocity_.y * sub_dt),
                z + col.offset_.z + (rb.velocity_.z * sub_dt)
            };

            collision_result result{};
            measure_into(detailed_active_, stats_.voxel_collision_ms, [&] {
                result = resolve_box_voxel(centre, half, rb.velocity_);
            });
            x    = result.resolved_position.x - col.offset_.x;
            z    = result.resolved_position.z - col.offset_.z;
            feet = result.resolved_position.y - half.y;

            if (result.grounded) {
                riding         = true;
                rb.velocity_.y = 0.0f;
            }
        }

        vec3f pushed{x, feet + feet_below, z};
        measure_into(detailed_active_, stats_.entity_collision_ms, [&] {
            resolve_entity_collisions(ent, pushed, rb.velocity_, half, col.offset_);
        });
        x = pushed.x;
        z = pushed.z;
        if (!riding) {
            feet = pushed.y - feet_below;
        }
    }

    const bool looking = riding && lead > ride_look_past;
    const vec3f reach{heading.x * lead, 0.0f, heading.z * lead};
    const vec3f corner{x + col.offset_.x - half.x, feet - leg, z + col.offset_.z - half.z};
    rb.ride_probe_ = {
        .ahead_corner  = corner + reach,
        .behind_corner = corner - reach,
        .size          = {col.extents_.x, 2.0f * leg, col.extents_.z},
        .looks_ahead   = looking && may_climb,
        .looks_behind  = looking,
    };

    rb.grounded_ = riding;
    return {x, feet + feet_below, z};
}

// см. docs/ENGINE.md#положение-для-показа
auto physics_system::show_between_steps_(float32 frame_seconds) -> void {
    const float32 into_step = std::clamp(accumulated_time_ / fixed_dt, 0.0f, 1.0f);
    const float32 behind    = 1.0f - into_step;

    auto& reg        = world_->registry();
    auto& transforms = world_->system<transform_system>();
    const auto voxel = static_cast<float32>(
        world_->system<world_grid_system>().grid()->world_units_per_voxel()
    );

    for (auto [ent, rb, tc] : reg.view<rigid_body_component, transform_component>()) {
        const vec3f back = rb.stepped_from_ - rb.stepped_to_;

        const bool moved_by_hand = tc.get_position() != rb.stepped_to_;
        const bool too_far =
            math::dot(back, back) > (longest_shown_step * voxel) * (longest_shown_step * voxel);

        const bool held    = moved_by_hand || too_far || rb.frozen_;
        const vec3f offset = held ? vec3f{0.0f, 0.0f, 0.0f} : back * behind;
        if (tc.get_shown_offset() != offset) {
            transforms.modify(ent).set_shown_offset(offset);
        }

        // см. docs/ENGINE.md#модель-опускается-мягче-тела
        float32 lift = 0.0f;
        if (rb.model_sink_seconds_ > 0.0f &&
            reg.try_get<transform_component>(rb.model_node_) != nullptr) {
            const float32 shown = tc.get_position().y + offset.y;
            if (!rb.model_height_known_ || held) {
                rb.model_height_       = shown;
                rb.model_height_known_ = true;
            } else {
                const float32 seconds =
                    rb.grounded_ ? rb.model_sink_seconds_ : rb.model_air_sink_seconds_;
                const float32 closer =
                    seconds > 0.0f ? 1.0f - std::exp(-frame_seconds / seconds) : 1.0f;
                rb.model_height_ += (shown - rb.model_height_) * closer;
                rb.model_height_ =
                    std::clamp(rb.model_height_, shown, shown + rb.model_lift_limit_);
            }
            lift = rb.model_height_ - shown;
        } else {
            rb.model_height_known_ = false;
        }
        if (lift != rb.model_lift_) {
            if (auto* follower = reg.try_get<transform_component>(rb.model_node_)) {
                const vec3f at = follower->get_position();
                transforms.modify(rb.model_node_)
                    .set_position({at.x, at.y + (lift - rb.model_lift_), at.z});
            }
            rb.model_lift_ = lift;
        }
    }
}

auto physics_system::are_chunks_loaded(
    const vec3f& position, const vec3f& extents
) const -> bool {
    auto* grid = world_->system<world_grid_system>().grid();
    const auto vs = static_cast<float32>(grid->world_units_per_voxel());
    auto half = extents * 0.5f;

    auto min_world = vec3i{
        static_cast<int32>(std::floor((position.x - half.x) / vs) * vs),
        0,
        static_cast<int32>(std::floor((position.z - half.z) / vs) * vs)
    };
    auto max_world = vec3i{
        static_cast<int32>(std::floor((position.x + half.x) / vs) * vs),
        0,
        static_cast<int32>(std::floor((position.z + half.z) / vs) * vs)
    };

    auto min_chunk = grid->world_to_chunk_coord(min_world);
    auto max_chunk = grid->world_to_chunk_coord(max_world);

    for (int32 cx = min_chunk.x; cx <= max_chunk.x; ++cx) {
        for (int32 cz = min_chunk.z; cz <= max_chunk.z; ++cz) {
            if (!grid->has_column({cx, cz})) {
                return false;
            }
        }
    }

    return true;
}

auto physics_system::resolve_box_voxel(
    vec3f center, const vec3f& half_extents, vec3f& velocity
) const -> collision_result {
    auto* grid = world_->system<world_grid_system>().grid();
    auto vs = static_cast<float32>(grid->world_units_per_voxel());
    auto vs_i = grid->world_units_per_voxel();
    bool grounded = false;

    vec3i cached_coord{std::numeric_limits<int32>::min(), 0, 0};
    chunk* cached_chunk = nullptr;

    const auto holds = [&](int32 vx, int32 vy, int32 vz) -> bool {
        const vec3i world_pos{vx * vs_i, vy * vs_i, vz * vs_i};

        const auto chunk_coord = grid->world_to_chunk_coord(world_pos);
        if (chunk_coord != cached_coord) {
            cached_coord = chunk_coord;
            cached_chunk = grid->get_chunk(chunk_coord);
        }
        return cached_chunk != nullptr &&
               !cached_chunk->get_voxel(grid->world_to_local_coord(world_pos) / vs_i).is_empty();
    };

    for (int32 iter = 0; iter < max_collision_iterations; ++iter) {
        auto entity_min = center - half_extents;
        auto entity_max = center + half_extents;

        auto min_vx = static_cast<int32>(std::floor(entity_min.x / vs));
        auto min_vy = static_cast<int32>(std::floor(entity_min.y / vs));
        auto min_vz = static_cast<int32>(std::floor(entity_min.z / vs));
        auto max_vx = static_cast<int32>(std::floor(entity_max.x / vs));
        auto max_vy = static_cast<int32>(std::floor(entity_max.y / vs));
        auto max_vz = static_cast<int32>(std::floor(entity_max.z / vs));

        float32 min_penetration = std::numeric_limits<float32>::max();
        vec3f push_direction{0.0f, 0.0f, 0.0f};
        bool found_collision = false;

        for (int32 vx = min_vx; vx <= max_vx; ++vx) {
            for (int32 vy = min_vy; vy <= max_vy; ++vy) {
                for (int32 vz = min_vz; vz <= max_vz; ++vz) {
                    if (!holds(vx, vy, vz)) {
                        continue;
                    }

                    auto voxel_min = vec3f{
                        static_cast<float32>(vx) * vs,
                        static_cast<float32>(vy) * vs,
                        static_cast<float32>(vz) * vs
                    };
                    auto voxel_max = vec3f{
                        voxel_min.x + vs,
                        voxel_min.y + vs,
                        voxel_min.z + vs
                    };

                    auto overlap_x = std::min(entity_max.x, voxel_max.x) - std::max(entity_min.x, voxel_min.x);
                    auto overlap_y = std::min(entity_max.y, voxel_max.y) - std::max(entity_min.y, voxel_min.y);
                    auto overlap_z = std::min(entity_max.z, voxel_max.z) - std::max(entity_min.z, voxel_min.z);

                    if (overlap_x <= 0.0f || overlap_y <= 0.0f || overlap_z <= 0.0f) {
                        continue;
                    }

                    auto voxel_center = (voxel_min + voxel_max) * 0.5f;
                    auto dir = center - voxel_center;

                    // см. docs/ENGINE.md#стык-вокселей-не-толкает-вбок
                    const int32 out_x = dir.x >= 0.0f ? 1 : -1;
                    const int32 out_y = dir.y >= 0.0f ? 1 : -1;
                    const int32 out_z = dir.z >= 0.0f ? 1 : -1;

                    const bool open_x = !holds(vx + out_x, vy, vz);
                    const bool open_y = !holds(vx, vy + out_y, vz);
                    const bool open_z = !holds(vx, vy, vz + out_z);
                    const bool buried = !open_x && !open_y && !open_z;

                    constexpr float32 no_way = std::numeric_limits<float32>::max();
                    const float32 cost_x = open_x || buried ? overlap_x : no_way;
                    const float32 cost_y = open_y || buried ? overlap_y : no_way;
                    const float32 cost_z = open_z || buried ? overlap_z : no_way;

                    vec3f push{0.0f, 0.0f, 0.0f};
                    float32 penetration = 0.0f;

                    if (cost_x <= cost_y && cost_x <= cost_z) {
                        penetration = overlap_x;
                        push.x      = static_cast<float32>(out_x);
                    } else if (cost_y <= cost_x && cost_y <= cost_z) {
                        penetration = overlap_y;
                        push.y      = static_cast<float32>(out_y);
                    } else {
                        penetration = overlap_z;
                        push.z      = static_cast<float32>(out_z);
                    }

                    if (penetration < min_penetration) {
                        min_penetration = penetration;
                        push_direction = push;
                        found_collision = true;
                    }
                }
            }
        }

        if (!found_collision) {
            break;
        }

        center = center + push_direction * min_penetration;

        auto vel_along_normal = math::dot(velocity, push_direction);
        if (vel_along_normal < 0.0f) {
            velocity = velocity - push_direction * vel_along_normal;
        }

        if (push_direction.y > 0.7f && velocity.y <= 0.0f) {
            grounded = true;
        }
    }

    return {center, grounded};
}

auto physics_system::resolve_entity_collisions(
    entity ent, vec3f& position, vec3f& velocity,
    const vec3f& half_extents, const vec3f& offset
) -> void {
    auto center = position + offset;
    spatial::aabb entity_aabb{center - half_extents, center + half_extents};

    measure_into(detailed_active_, stats_.entity_query_ms, [&] {
        world_->system<spatial_system>().query_all(
            entity_aabb, entity_query_cache_, spatial_layer::character);
    });
    stats_.entity_query_results += static_cast<int32>(entity_query_cache_.size());

    auto& reg = world_->registry();
    measure_into(detailed_active_, stats_.entity_resolve_ms, [&] {
        for (const auto other : entity_query_cache_) {
            if (other == ent) {
                continue;
            }

            const auto* other_col_ptr = reg.try_get<box_collider_component>(other);
            const auto* other_tc_ptr  = reg.try_get<transform_component>(other);
            if (other_col_ptr == nullptr || other_tc_ptr == nullptr) {
                continue;
            }

            const auto& other_col = *other_col_ptr;
            const auto& other_tc  = *other_tc_ptr;

            auto other_half   = other_col.extents_ * 0.5f;
            auto other_center = other_tc.get_position() + other_col.offset_;

            auto overlap_x = std::min(center.x + half_extents.x, other_center.x + other_half.x)
                           - std::max(center.x - half_extents.x, other_center.x - other_half.x);
            auto overlap_y = std::min(center.y + half_extents.y, other_center.y + other_half.y)
                           - std::max(center.y - half_extents.y, other_center.y - other_half.y);
            auto overlap_z = std::min(center.z + half_extents.z, other_center.z + other_half.z)
                           - std::max(center.z - half_extents.z, other_center.z - other_half.z);

            if (overlap_x <= 0.0f || overlap_y <= 0.0f || overlap_z <= 0.0f) {
                continue;
            }

            auto dir = center - other_center;

            vec3f push{0.0f, 0.0f, 0.0f};
            float32 penetration = 0.0f;

            if (overlap_x <= overlap_y && overlap_x <= overlap_z) {
                penetration = overlap_x;
                push.x = dir.x >= 0.0f ? 1.0f : -1.0f;
            } else if (overlap_y <= overlap_x && overlap_y <= overlap_z) {
                penetration = overlap_y;
                push.y = dir.y >= 0.0f ? 1.0f : -1.0f;
            } else {
                penetration = overlap_z;
                push.z = dir.z >= 0.0f ? 1.0f : -1.0f;
            }

            center = center + push * penetration;
            entity_aabb = {center - half_extents, center + half_extents};

            auto vel_along = math::dot(velocity, push);
            if (vel_along < 0.0f) {
                velocity = velocity - push * vel_along;
            }
        }

    });
    position = center - offset;
}

physics_system::rigid_body_modifier::rigid_body_modifier(
    physics_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto physics_system::modify(
    entity ent
) -> rigid_body_modifier {
    return rigid_body_modifier(this, ent);
}

auto physics_system::rigid_body_modifier::set_velocity(
    const vec3f& vel
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.velocity_ = vel;
    return *this;
}

auto physics_system::rigid_body_modifier::soften_descent(
    entity model, float32 seconds, float32 seconds_in_the_air, float32 lift_limit
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.model_node_             = model;
    comp.model_sink_seconds_     = std::max(seconds, 0.0f);
    comp.model_air_sink_seconds_ = std::max(seconds_in_the_air, 0.0f);
    comp.model_lift_limit_       = std::max(lift_limit, 0.0f);
    return *this;
}

auto physics_system::rigid_body_modifier::set_gravity_scale(
    float32 scale
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.gravity_scale_ = scale;
    return *this;
}

auto physics_system::rigid_body_modifier::add_impulse(
    const vec3f& impulse
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.velocity_ = comp.velocity_ + impulse;
    return *this;
}

auto physics_system::rigid_body_modifier::add_external_impulse(
    const vec3f& impulse
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.impulse_ = comp.impulse_ + impulse;
    return *this;
}

auto physics_system::rigid_body_modifier::set_drag(
    float32 drag
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.drag_ = drag;
    return *this;
}

physics_system::collider_modifier::collider_modifier(
    physics_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto physics_system::modify_collider(
    entity ent
) -> collider_modifier {
    return collider_modifier(this, ent);
}

auto physics_system::collider_modifier::set_extents(
    const vec3f& ext
) -> collider_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<box_collider_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<box_collider_component>(entity_);
    comp.extents_ = ext;
    return *this;
}

auto physics_system::collider_modifier::set_offset(
    const vec3f& offset
) -> collider_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<box_collider_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<box_collider_component>(entity_);
    comp.offset_ = offset;
    return *this;
}

}  // namespace vw::ecs
