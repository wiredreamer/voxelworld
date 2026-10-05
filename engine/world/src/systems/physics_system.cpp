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

            new_position = position;
            rb.grounded_ = false;
            for (int32 sub = 0; sub < substeps; ++sub) {
                new_position = new_position + rb.velocity_ * sub_dt;

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

        world_->system<transform_system>().modify(ent).set_position(new_position);
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
                    auto world_pos = vec3i{vx * vs_i, vy * vs_i, vz * vs_i};

                    const auto chunk_coord = grid->world_to_chunk_coord(world_pos);
                    if (chunk_coord != cached_coord) {
                        cached_coord = chunk_coord;
                        cached_chunk = grid->get_chunk(chunk_coord);
                    }
                    if (cached_chunk == nullptr) {
                        continue;
                    }

                    auto v = cached_chunk->get_voxel(grid->world_to_local_coord(world_pos) / vs_i);
                    if (v.is_empty()) {
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
