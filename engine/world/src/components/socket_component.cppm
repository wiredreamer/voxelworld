export module vw.world:components.socket;

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

struct socket_point {
    std::string name;
    vec3f position{0.0F, 0.0F, 0.0F};
    quat rotation;
    vec3f scale{1.0F, 1.0F, 1.0F};
    entity attached = invalid_entity;
};

struct socket_component final {
    socket_component() = default;

    explicit socket_component(std::vector<socket_point> sockets) : sockets_(std::move(sockets)) {}

    [[nodiscard]] auto get_sockets() const -> const std::vector<socket_point>& {
        return sockets_;
    }

    [[nodiscard]] auto find(const std::string& name) const -> const socket_point* {
        const auto it =
            std::ranges::find_if(sockets_, [&](const socket_point& sp) { return sp.name == name; });
        return it != sockets_.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] auto is_occupied(const std::string& name) const -> bool {
        const auto* sp = find(name);
        return sp != nullptr && sp->attached.is_valid();
    }

private:
    friend class socket_system;

    std::vector<socket_point> sockets_;
};

}  // namespace vw::ecs
