export module vw.world:systems.spatial;

import std;

import vw.core;
import vw.ecs;
import :components;
import :grid;
import :spatial;
import :light;
import :terrain;

export namespace vw::ecs {

class world;

struct voxel_ray_hit {
    entity ent;
    vec3i voxel_pos;
    vec3i empty_pos;
};

class spatial_system {
public:
    static constexpr std::string_view system_name = "spatial";

    explicit spatial_system(world& w);

    auto update(float32 dt) -> void;

    auto query_all(const spatial::frustum& f, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all(const spatial::ray& r, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all(const spatial::aabb& bounds, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all_any(std::span<const spatial::frustum> frustums,
                       std::vector<entity>& result_out) const -> void;

    [[nodiscard]] auto voxel_ray_cast(const spatial::ray& r, std::vector<entity>& candidates,
                                      spatial_layer_mask layer_mask = spatial_layer::all) const
        -> std::optional<voxel_ray_hit>;

    auto cleanup(entity ent) -> void;

    template <typename C>
        requires std::same_as<C, spatial_component>
    auto on_remove(entity e) -> void {
        cleanup(e);
    }

    class spatial_modifier {
    public:
        auto set_layer(spatial_layer_mask layer) -> spatial_modifier&;

    private:
        friend class spatial_system;
        spatial_modifier(spatial_system* system, entity ent);

        spatial_system* system_;
        entity entity_;
    };

    auto modify(entity ent) -> spatial_modifier;

private:
    auto update_entity(entity ent) -> void;
    auto calculate_aabb_from_model(entity ent, const model_component& model_comp,
                                   const transform_component& transform_comp) const
        -> spatial::aabb;
    static auto calculate_aabb_from_collider(const box_collider_component& collider,
                                             const transform_component& transform_comp)
        -> spatial::aabb;
    static auto expand_aabb_for_fat(const spatial::aabb& bounds) -> spatial::aabb;

    world* world_;
    dynamic_aabb_tree tree_;
};

}  // namespace vw::ecs
