export module vw.world:systems.transform;

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

class transform_system final {
public:
    static constexpr std::string_view system_name = "transform";

    explicit transform_system(world& w);

    auto update(float32 dt) -> void;

    class transform_modifier {
    public:
        auto set_transform(const transform& transform) -> transform_modifier&;
        auto set_transform_with_matrix(const transform& transform, const mat4f& local_matrix)
            -> transform_modifier&;
        auto set_position(const vec3f& position) -> transform_modifier&;
        auto set_rotation(const quat& rotation) -> transform_modifier&;
        auto set_rotation_euler(const vec3f& euler) -> transform_modifier&;
        auto set_scale(const vec3f& scale) -> transform_modifier&;
        auto translate(const vec3f& offset) -> transform_modifier&;
        auto rotate(const vec3f& angles) -> transform_modifier&;
        auto scale(const vec3f& factor) -> transform_modifier&;
        auto mark_world_dirty() -> transform_modifier&;

    private:
        friend class transform_system;
        transform_modifier(transform_system* system, entity ent);

        transform_system* system_;
        entity entity_;
    };

    [[nodiscard]] auto modify(entity ent) -> transform_modifier;

    template <typename C>
        requires(std::same_as<C, transform_component> || std::same_as<C, spatial_component>)
    auto on_add(entity e) -> void;

private:
    auto mark_children_world_dirty(entity ent) -> void;
    auto update_entity_world_matrix(entity ent, const transform_component& transform_comp) -> void;

    world* world_;
    uint32 subtree_stamp_ = 1;
    std::vector<std::pair<std::size_t, entity>> sorted_entities_;
};

}  // namespace vw::ecs
