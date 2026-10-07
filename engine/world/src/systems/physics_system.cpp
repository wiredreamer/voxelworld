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

    show_between_steps_();

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

        rb.stepped_from_     = position;
        rb.stepped_to_       = position;
        rb.sink_before_step_ = rb.step_sink_;

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

            rb.frozen_ = false;

            const auto voxel_size =
                static_cast<float32>(world_->system<world_grid_system>().grid()->world_units_per_voxel());
            const auto substeps = std::max(
                1, static_cast<int32>(std::ceil(math::length(rb.velocity_ * dt) / (voxel_size * max_substep_voxels)))
            );
            const float32 sub_dt = dt / static_cast<float32>(substeps);

            const auto* walker   = reg.try_get<character_controller_component>(ent);
            const float32 step_height =
                walker != nullptr ? walker->get_step_hop_voxels() * voxel_size : 0.0f;
            const bool stood = rb.grounded_;
            float32 stepped  = 0.0f;

            new_position = position;
            rb.grounded_ = false;
            for (int32 sub = 0; sub < substeps; ++sub) {
                const vec3f before = new_position;
                const vec3f wanted = rb.velocity_ * sub_dt;

                new_position = before + wanted;

                collision_result result{};
                measure_into(detailed_active_, stats_.voxel_collision_ms, [&] {
                    result = resolve_box_voxel(new_position + col.offset_, half, rb.velocity_);
                });

                new_position = result.resolved_position - col.offset_;
                rb.grounded_ = rb.grounded_ || result.grounded;

                // см. docs/ENGINE.md#шаг-на-ступень
                if (step_height > 0.0f && (stood || rb.grounded_) && wanted.y <= 0.0f) {
                    const vec3f lost{
                        wanted.x - (new_position.x - before.x), 0.0f,
                        wanted.z - (new_position.z - before.z)
                    };
                    const float32 planar = (wanted.x * wanted.x) + (wanted.z * wanted.z);

                    if (planar > 0.0f &&
                        math::dot(lost, lost) > planar * step_blocked_share * step_blocked_share) {
                        const auto stood_on = step_up_(before + col.offset_, half, wanted, step_height);
                        if (stood_on) {
                            const vec3f onto = *stood_on - col.offset_;

                            stepped += onto.y - before.y;
                            ++rb.steps_taken_;

                            new_position   = onto;
                            rb.velocity_.x = wanted.x / sub_dt;
                            rb.velocity_.z = wanted.z / sub_dt;
                            rb.velocity_.y = 0.0f;
                            rb.grounded_   = true;
                        }
                    }
                }

                measure_into(detailed_active_, stats_.entity_collision_ms, [&] {
                    resolve_entity_collisions(ent, new_position, rb.velocity_, half, col.offset_);
                });
            }

            // см. docs/ENGINE.md#модель-поднимается-заранее
            if (rb.step_smooth_seconds_ > 0.0f) {
                const vec3f planar{rb.velocity_.x, 0.0f, rb.velocity_.z};
                const float32 speed = math::length(planar);

                float32 lead = 0.0f;
                if (step_height > 0.0f && rb.grounded_ && speed > 1.0f) {
                    lead = rise_coming_(
                        new_position + col.offset_, half, planar * (1.0f / speed), step_height,
                        voxel_size
                    );
                }

                const float32 natural = step_lead_slack * speed * dt * (step_height / voxel_size);
                const float32 jump    = (lead - rb.step_lead_) + stepped;
                const float32 kept    = std::clamp(jump, -natural, natural);

                rb.step_catch_up_ = (rb.step_catch_up_ - (jump - kept)) *
                                    std::exp(-dt / rb.step_smooth_seconds_);
                rb.step_lead_ = lead;
                rb.step_sink_ = rb.step_lead_ + rb.step_catch_up_;
            } else {
                rb.step_sink_     = 0.0f;
                rb.step_lead_     = 0.0f;
                rb.step_catch_up_ = 0.0f;
            }
        }

        rb.stepped_to_ = new_position;
        world_->system<transform_system>().modify(ent).set_position(new_position);
    }
}

// см. docs/ENGINE.md#положение-для-показа
auto physics_system::show_between_steps_() -> void {
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

        const float32 sink =
            held ? rb.step_sink_
                 : rb.step_sink_ + ((rb.sink_before_step_ - rb.step_sink_) * behind);
        if (sink != rb.shown_sink_) {
            if (auto* follower = reg.try_get<transform_component>(rb.step_follower_)) {
                const vec3f at = follower->get_position();
                transforms.modify(rb.step_follower_)
                    .set_position({at.x, at.y + (sink - rb.shown_sink_), at.z});
            }
            rb.shown_sink_ = sink;
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

auto physics_system::box_blocked_(const vec3f& lo, const vec3f& hi) const -> bool {
    auto* grid       = world_->system<world_grid_system>().grid();
    const int32 vs_i = grid->world_units_per_voxel();
    const auto vs    = static_cast<float32>(vs_i);

    const auto first = [vs](float32 v) -> int32 { return static_cast<int32>(std::floor(v / vs)); };
    const auto last  = [vs](float32 v) -> int32 {
        return static_cast<int32>(std::ceil(v / vs)) - 1;
    };

    for (int32 vx = first(lo.x); vx <= last(hi.x); ++vx) {
        for (int32 vy = first(lo.y); vy <= last(hi.y); ++vy) {
            for (int32 vz = first(lo.z); vz <= last(hi.z); ++vz) {
                if (!grid->get_voxel(vec3i{vx * vs_i, vy * vs_i, vz * vs_i}).is_empty()) {
                    return true;
                }
            }
        }
    }
    return false;
}

auto physics_system::rise_coming_(
    const vec3f& center, const vec3f& half_extents, const vec3f& heading, float32 step_height,
    float32 within
) const -> float32 {
    constexpr float32 skin = 0.01f;

    const vec3f shrunk = half_extents - vec3f{skin, skin, skin};
    const auto blocked_at = [&](float32 along) -> bool {
        const vec3f at = center + (heading * along);
        return box_blocked_(at - shrunk, at + shrunk);
    };

    if (!blocked_at(within)) {
        return 0.0f;
    }

    float32 free_until = 0.0f;
    float32 blocked_from = within;
    for (int32 halving = 0; halving < step_lead_halvings; ++halving) {
        const float32 middle = 0.5f * (free_until + blocked_from);
        if (blocked_at(middle)) {
            blocked_from = middle;
        } else {
            free_until = middle;
        }
    }

    const vec3f touching = center + (heading * free_until);
    const auto stood_on  = step_up_(
        touching, half_extents, heading * ((blocked_from - free_until) + (2.0f * skin)), step_height
    );
    if (!stood_on) {
        return 0.0f;
    }

    return (stood_on->y - center.y) * (1.0f - (free_until / within));
}

auto physics_system::step_up_(
    const vec3f& center, const vec3f& half_extents, const vec3f& wanted, float32 step_height
) const -> std::optional<vec3f> {
    constexpr float32 skin = 0.01f;

    auto* grid       = world_->system<world_grid_system>().grid();
    const int32 vs_i = grid->world_units_per_voxel();
    const auto vs    = static_cast<float32>(vs_i);

    const auto first = [vs](float32 v) -> int32 { return static_cast<int32>(std::floor(v / vs)); };
    const auto last  = [vs](float32 v) -> int32 {
        return static_cast<int32>(std::ceil(v / vs)) - 1;
    };
    const auto holds = [&](int32 vx, int32 vy, int32 vz) -> bool {
        return !grid->get_voxel(vec3i{vx * vs_i, vy * vs_i, vz * vs_i}).is_empty();
    };
    const auto blocked = [&](const vec3f& lo, const vec3f& hi) -> bool {
        for (int32 vx = first(lo.x); vx <= last(hi.x); ++vx) {
            for (int32 vy = first(lo.y); vy <= last(hi.y); ++vy) {
                for (int32 vz = first(lo.z); vz <= last(hi.z); ++vz) {
                    if (holds(vx, vy, vz)) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    const vec3f shrunk = half_extents - vec3f{skin, skin, skin};
    const vec3f raised = center + vec3f{0.0f, step_height + skin, 0.0f};
    if (blocked(raised - shrunk, raised + shrunk)) {
        return std::nullopt;
    }

    const std::array<vec3f, 3> ways{
        vec3f{wanted.x, 0.0f, wanted.z}, vec3f{wanted.x, 0.0f, 0.0f}, vec3f{0.0f, 0.0f, wanted.z}
    };

    for (const vec3f& way : ways) {
        if (way.x == 0.0f && way.z == 0.0f) {
            continue;
        }

        const vec3f ahead = raised + way;
        const vec3f lo    = ahead - shrunk;
        const vec3f hi    = ahead + shrunk;
        if (blocked(lo, hi)) {
            continue;
        }

        const float32 feet_then = ahead.y - half_extents.y;
        const float32 feet_now  = center.y - half_extents.y;

        for (int32 vy = last(feet_then); vy >= first(feet_now + skin); --vy) {
            bool stands = false;
            for (int32 vx = first(lo.x); vx <= last(hi.x) && !stands; ++vx) {
                for (int32 vz = first(lo.z); vz <= last(hi.z) && !stands; ++vz) {
                    stands = holds(vx, vy, vz);
                }
            }
            if (stands) {
                const float32 top = static_cast<float32>(vy + 1) * vs;
                return vec3f{ahead.x, top + half_extents.y, ahead.z};
            }
        }
    }

    return std::nullopt;
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

auto physics_system::rigid_body_modifier::smooth_steps(
    entity follower, float32 seconds
) -> rigid_body_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<rigid_body_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<rigid_body_component>(entity_);
    comp.step_follower_       = follower;
    comp.step_smooth_seconds_ = seconds;
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
