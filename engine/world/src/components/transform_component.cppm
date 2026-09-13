export module vw.world:components.transform;

import std;

import vw.core;
import vw.ecs;
import :spatial;

export namespace vw::ecs {

class animation_fsm_system;
class animation_system;
class character_controller_system;
class hierarchy_system;
class light_system;
class model_system;
class physics_system;
class socket_system;
class spatial_system;
class transform_system;
class world_grid_system;

struct transform_component final {
    [[nodiscard]] auto get_local_matrix() const -> mat4f {
        if (local_dirty_) {
            local_matrix_ = transform_.calc_matrix();
            local_dirty_  = false;
        }
        return local_matrix_;
    }

    [[nodiscard]] auto get_world_matrix() const -> mat4f {
        return world_matrix_;
    }

    [[nodiscard]] auto get_transform() const -> const transform& {
        return transform_;
    }

    [[nodiscard]] auto get_position() const -> const vec3f& {
        return transform_.get_position();
    }

    [[nodiscard]] auto get_rotation() const -> const quat& {
        return transform_.get_rotation();
    }

    [[nodiscard]] auto get_rotation_euler() const -> vec3f {
        return transform_.get_rotation_euler();
    }

    [[nodiscard]] auto get_scale() const -> const vec3f& {
        return transform_.get_scale();
    }

    [[nodiscard]] auto get_origin() const -> const vec3f& {
        return transform_.get_origin();
    }

private:
    friend class hierarchy_system;
    friend class transform_system;

    transform transform_;

    mutable mat4f local_matrix_;
    mutable bool local_dirty_ = true;

    mutable mat4f world_matrix_;
    mutable bool world_dirty_ = true;
};

struct hierarchy_component final {
    [[nodiscard]] auto has_parent() const -> bool {
        return parent_.is_valid();
    }

    [[nodiscard]] auto get_parent() const -> entity {
        return parent_;
    }

    [[nodiscard]] auto has_child(entity child) const -> bool {
        return std::ranges::contains(children_, child);
    }

    [[nodiscard]] auto get_children() const -> const std::vector<entity>& {
        return children_;
    }

private:
    friend class hierarchy_system;
    friend class transform_system;

    entity parent_;
    std::vector<entity> children_;
};

}  // namespace vw::ecs
