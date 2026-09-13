export module vw.world:systems.socket;

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

class socket_system final {
public:
    static constexpr std::string_view system_name = "socket";

    explicit socket_system(world& w);

    class socket_modifier {
    public:
        auto attach(const std::string& socket_name, entity child) -> socket_modifier&;
        auto detach(const std::string& socket_name) -> socket_modifier&;
        auto add_socket(const std::string& name, const vec3f& position = {},
                        const quat& rotation = {}, const vec3f& scale = vec3f{1, 1, 1})
            -> socket_modifier&;
        auto remove_socket(const std::string& name) -> socket_modifier&;

    private:
        friend class socket_system;
        socket_modifier(socket_system* system, entity ent);

        socket_system* system_;
        entity entity_;
    };

    auto modify(entity ent) -> socket_modifier;
    auto cleanup(entity ent) -> void;
    auto update(float32 dt) -> void;

    template <typename C>
        requires std::same_as<C, socket_component>
    auto on_remove(entity e) -> void {
        cleanup(e);
    }

private:
    world* world_;
};

}  // namespace vw::ecs
