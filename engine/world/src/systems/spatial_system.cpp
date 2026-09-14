module vw.world;

import std;
import vw.core;

namespace vw::ecs {

spatial_system::spatial_system(
    world& w
)
    : world_(&w) {}

auto spatial_system::modify(entity ent) -> spatial_modifier {
    return spatial_modifier(this, ent);
}

spatial_system::spatial_modifier::spatial_modifier(
    spatial_system* system, entity ent
)
    : system_(system)
    , entity_(ent) {}

auto spatial_system::spatial_modifier::set_layer(
    spatial_layer_mask layer
) -> spatial_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<spatial_component>(entity_)) {
        return *this;
    }
    auto& spatial = reg.get<spatial_component>(entity_);
    spatial.layer_ = layer;
    return *this;
}

auto spatial_system::update(float32 /*dt*/) -> void {
    auto& reg       = world_->registry();
    auto& requested = reg.requested<spatial_component>();
    if (requested.empty()) {
        return;
    }

    for (entity ent : requested) {
        // Границы есть у того, чью форму движок знает: у несущего модель — по
        // её вокселям, у несущего коллайдер — по коробке физики. Второе — про
        // корень иерархии, который сам ничего не рисует: тело, собранное из
        // детей, в дерево не попадало вовсе, и запрос столкновений его не
        // находил, сколько бы слоёв ему ни назначили.
        const bool can_be_updated =  //
            reg.has<transform_component>(ent) &&
            reg.has<spatial_component>(ent) &&
            (reg.has<model_component>(ent) || reg.has<box_collider_component>(ent));
        if (!can_be_updated) {
            continue;
        }
        update_entity(ent);
        reg.notify_changed<spatial_component>(ent);
    }

    reg.clear_requested<spatial_component>();
}

auto spatial_system::update_entity(
    entity ent
) -> void {
    auto& reg     = world_->registry();
    auto& spatial = reg.get<spatial_component>(ent);

    const auto& transform_comp = reg.get<transform_component>(ent);

    // Модель важнее коллайдера у того, у кого есть и то, и другое: по её
    // границам такую сущность видели до сих пор, и менять их значило бы менять
    // отсев и попадания луча заодно.
    const spatial::aabb new_bounds = reg.has<model_component>(ent)
        ? calculate_aabb_from_model(ent, reg.get<model_component>(ent), transform_comp)
        : calculate_aabb_from_collider(reg.get<box_collider_component>(ent), transform_comp);

    const bool bounds_changed =  //
        spatial.bounds_.min != new_bounds.min || spatial.bounds_.max != new_bounds.max;

    if (bounds_changed) {
        const bool needs_tree_update = spatial.dirty_ || new_bounds.min.x < spatial.fat_bounds_.min.x ||
            new_bounds.min.y < spatial.fat_bounds_.min.y ||
            new_bounds.min.z < spatial.fat_bounds_.min.z ||
            new_bounds.max.x > spatial.fat_bounds_.max.x ||
            new_bounds.max.y > spatial.fat_bounds_.max.y ||
            new_bounds.max.z > spatial.fat_bounds_.max.z;

        if (needs_tree_update) {
            spatial::aabb new_fat_bounds = expand_aabb_for_fat(new_bounds);

            if (!spatial.dirty_) {
                tree_.update(ent, new_fat_bounds, spatial.layer_);
            } else {
                tree_.insert(ent, new_fat_bounds, spatial.layer_);
            }

            spatial.bounds_     = new_bounds;
            spatial.fat_bounds_ = new_fat_bounds;
            spatial.dirty_      = false;
        } else {
            spatial.bounds_ = new_bounds;
            spatial.dirty_  = false;
        }
    } else {
        spatial.dirty_ = false;
    }
}

auto spatial_system::expand_aabb_for_fat(
    const spatial::aabb& bounds
) -> spatial::aabb {
    constexpr float expansion_factor = 0.1f;
    constexpr float min_expansion    = 0.1f;

    const vec3f size = bounds.size();
    vec3f expansion{
        std::max(size.x * expansion_factor, min_expansion),
        std::max(size.y * expansion_factor, min_expansion),
        std::max(size.z * expansion_factor, min_expansion)
    };

    return spatial::aabb{
        vec3f{bounds.min.x - expansion.x, bounds.min.y - expansion.y, bounds.min.z - expansion.z},
        vec3f{bounds.max.x + expansion.x, bounds.max.y + expansion.y, bounds.max.z + expansion.z}
    };
}

auto spatial_system::calculate_aabb_from_collider(
    const box_collider_component& collider, const transform_component& transform_comp
) -> spatial::aabb {
    // Коробка физики осями мира и живёт: resolve_box_voxel вращения не знает и
    // берёт середину как позицию плюс смещение. Границы, посчитанные через
    // мировую матрицу, описывали бы не то тело, с которым столкнётся движок.
    const vec3f centre = transform_comp.get_position() + collider.get_offset();
    const vec3f half   = collider.get_extents() * 0.5f;

    return spatial::aabb{centre - half, centre + half};
}

auto spatial_system::calculate_aabb_from_model(
    entity, const model_component& model_comp, const transform_component& transform_comp
) const -> spatial::aabb {
    if (!model_comp.has_model()) {
        return spatial::aabb{.min={0.0f, 0.0f, 0.0f}, .max={0.0f, 0.0f, 0.0f}};
    }

    // Размер в вокселях, без voxel_scale: масштаб уже несёт мировая матрица —
    // chunk::create_entity_ кладёт его туда, и вершинный шейдер растит ей же
    // воксельные координаты квада. Домножение здесь применяло его дважды, и
    // чанк арены получал коробку в шестнадцать раз больше себя: отсев фрустумом
    // брал в кадр всё подряд, а направления -X, -Y и -Z не срезались никогда,
    // потому что дальняя стенка коробки стояла далеко за чанком.
    const vec3i model_size = model_comp.size();

    constexpr vec3f local_min{0.0f, 0.0f, 0.0f};
    const vec3f local_max{
        static_cast<float32>(model_size.x),
        static_cast<float32>(model_size.y),
        static_cast<float32>(model_size.z)
    };

    const mat4f world_matrix = model_matrix(transform_comp, model_comp);

    const std::array vertices = {
        vec3f{local_min.x, local_min.y, local_min.z},
        vec3f{local_max.x, local_min.y, local_min.z},
        vec3f{local_max.x, local_max.y, local_min.z},
        vec3f{local_min.x, local_max.y, local_min.z},
        vec3f{local_min.x, local_min.y, local_max.z},
        vec3f{local_max.x, local_min.y, local_max.z},
        vec3f{local_max.x, local_max.y, local_max.z},
        vec3f{local_min.x, local_max.y, local_max.z}
    };

    vec3f min_point{std::numeric_limits<float>::max()};
    vec3f max_point{std::numeric_limits<float>::lowest()};

    for (auto vertice : vertices) {
        vec3f world_vertex = world_matrix * vertice;
        min_point.x        = std::min(min_point.x, world_vertex.x);
        min_point.y        = std::min(min_point.y, world_vertex.y);
        min_point.z        = std::min(min_point.z, world_vertex.z);

        max_point.x = std::max(max_point.x, world_vertex.x);
        max_point.y = std::max(max_point.y, world_vertex.y);
        max_point.z = std::max(max_point.z, world_vertex.z);
    }

    return spatial::aabb{min_point, max_point};
}

auto spatial_system::query_all(
    const spatial::frustum& f, std::vector<entity>& result_out, spatial_layer_mask layer_mask
) const -> void {
    tree_.query_all(f, result_out, layer_mask);
}

auto spatial_system::query_all_any(
    std::span<const spatial::frustum> frustums, std::vector<entity>& result_out
) const -> void {
    tree_.query_all_any(frustums, result_out);
    std::sort(result_out.begin(), result_out.end());
}

auto spatial_system::query_all(
    const spatial::ray& r, std::vector<entity>& result_out, spatial_layer_mask layer_mask
) const -> void {
    tree_.query_all(r, result_out, layer_mask);
}

auto spatial_system::query_all(
    const spatial::aabb& bounds, std::vector<entity>& result_out, spatial_layer_mask layer_mask
) const -> void {
    tree_.query_all(bounds, result_out, layer_mask);
}

auto spatial_system::cleanup(
    entity ent
) -> void {
    tree_.remove(ent);
}

auto spatial_system::voxel_ray_cast(
    const spatial::ray& r, std::vector<entity>& candidates, spatial_layer_mask layer_mask
) const -> std::optional<voxel_ray_hit> {
    query_all(r, candidates, layer_mask);

    auto& reg = world_->registry();
    std::optional<voxel_ray_hit> closest_hit;
    float closest_distance_sq = std::numeric_limits<float>::max();

    for (entity ent : candidates) {
        const bool can_be_processed =  //
            reg.has<model_component>(ent) &&
            reg.has<transform_component>(ent);
        if (!can_be_processed) {
            continue;
        }

        const auto& model_comp     = reg.get<model_component>(ent);
        const auto& transform_comp = reg.get<transform_component>(ent);

        if (!model_comp.has_model()) {
            continue;
        }

        const vec3i model_size = model_comp.size();

        if (model_size.x <= 0 || model_size.y <= 0 || model_size.z <= 0) {
            continue;
        }

        const int width  = model_size.x;
        const int height = model_size.y;
        const int depth  = model_size.z;

        const mat4f world_matrix    = model_matrix(transform_comp, model_comp);
        const auto inv_result       = math::inverse_matrix(world_matrix);
        const mat4f inverse_world   = inv_result.value_or(math::identity_matrix());
        const vec3f local_start     = inverse_world * r.start;
        const vec3f local_end       = inverse_world * r.end;
        const vec3f local_direction = math::normalize(local_end - local_start);

        const spatial::aabb model_aabb{
            .min=vec3f{-1.f, -1.f, -1.f},
            .max=vec3f{
                static_cast<float>(width) + 1.f,
                static_cast<float>(height) + 1.f,
                static_cast<float>(depth) + 1.f
            }
        };

        float t_entry = 0.0f;
        spatial::ray local_ray{local_start, local_end};
        if (!local_ray.intersects_at(model_aabb, t_entry)) {
            continue;
        }

        vec3f entry_point = local_ray.point_at(t_entry);

        entry_point.x = math::clamp(entry_point.x, -1.f, static_cast<float>(width) + 1.f);
        entry_point.y = math::clamp(entry_point.y, -1.f, static_cast<float>(height) + 1.f);
        entry_point.z = math::clamp(entry_point.z, -1.f, static_cast<float>(depth) + 1.f);

        int x = static_cast<int>(std::floor(entry_point.x));
        int y = static_cast<int>(std::floor(entry_point.y));
        int z = static_cast<int>(std::floor(entry_point.z));

        x = std::max(-1, std::min(width, x));
        y = std::max(-1, std::min(height, y));
        z = std::max(-1, std::min(depth, z));

        int prev_x = x;
        int prev_y = y;
        int prev_z = z;

        const int step_x = local_direction.x > 0.0f ? 1 : -1;
        const int step_y = local_direction.y > 0.0f ? 1 : -1;
        const int step_z = local_direction.z > 0.0f ? 1 : -1;

        float t_max_x = 0.0f;
        float t_max_y = 0.0f;
        float t_max_z = 0.0f;

        if (local_direction.x != 0.0f) {
            const auto next_boundary_x =  //
                static_cast<float>(step_x > 0 ? x + 1 : x);
            t_max_x = (next_boundary_x - entry_point.x) / local_direction.x;
        } else {
            t_max_x = std::numeric_limits<float>::max();
        }

        if (local_direction.y != 0.0f) {
            const auto next_boundary_y =  //
                static_cast<float>(step_y > 0 ? y + 1 : y);
            t_max_y = (next_boundary_y - entry_point.y) / local_direction.y;
        } else {
            t_max_y = std::numeric_limits<float>::max();
        }

        if (local_direction.z != 0.0f) {
            const auto next_boundary_z =  //
                static_cast<float>(step_z > 0 ? z + 1 : z);
            t_max_z = (next_boundary_z - entry_point.z) / local_direction.z;
        } else {
            t_max_z = std::numeric_limits<float>::max();
        }

        const float t_delta_x =  //
            local_direction.x != 0.0f ?
            static_cast<float>(step_x) / local_direction.x :
            std::numeric_limits<float>::max();
        const float t_delta_y =  //
            local_direction.y != 0.0f ?
            static_cast<float>(step_y) / local_direction.y :
            std::numeric_limits<float>::max();
        const float t_delta_z =  //
            local_direction.z != 0.0f ?
            static_cast<float>(step_z) / local_direction.z :
            std::numeric_limits<float>::max();

        constexpr int max_iterations = 10000;
        int iterations               = 0;

        while (iterations < max_iterations) {
            if (x < -1 || x > width || y < -1 || y > height || z < -1 || z > depth) {
                break;
            }

            bool is_out_of_bounds =  //
                x == -1 || x == width || y == -1 || y == height || z == -1 || z == depth;

            if (!is_out_of_bounds && !model_comp.is_empty(x, y, z)) {
                vec3f hit_point_local{
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f
                };
                vec3f hit_point_world = world_matrix * hit_point_local;
                float distance_sq     = math::length_squared(hit_point_world - r.start);

                if (distance_sq < closest_distance_sq) {
                    closest_distance_sq = distance_sq;

                    closest_hit = voxel_ray_hit{
                        .ent       = ent,
                        .voxel_pos = vec3i{x, y, z},
                        .empty_pos = vec3i{prev_x, prev_y, prev_z}
                    };
                }
                break;
            }

            prev_x = x;
            prev_y = y;
            prev_z = z;

            if (t_max_x < t_max_y && t_max_x < t_max_z) {
                x += step_x;
                t_max_x += t_delta_x;
            } else if (t_max_y < t_max_z) {
                y += step_y;
                t_max_y += t_delta_y;
            } else {
                z += step_z;
                t_max_z += t_delta_z;
            }

            ++iterations;
        }
    }

    return closest_hit;
}

}  // namespace vw::ecs
