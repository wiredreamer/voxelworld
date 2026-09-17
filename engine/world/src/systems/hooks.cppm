export module vw.world:systems.hooks;

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

template <typename S, typename C>
concept has_on_add = requires(S& s, entity e) { s.template on_add<C>(e); };

template <typename S, typename C>
concept has_on_remove = requires(S& s, entity e) { s.template on_remove<C>(e); };

template <typename S>
concept has_shutdown = requires(S& s) { s.shutdown(); };

}  // namespace vw::ecs

namespace vw::ecs::detail {

template <typename C, typename S>
auto invoke_on_add(S& system, entity ent) -> void {
    if constexpr (has_on_add<S, C>) {
        system.template on_add<C>(ent);
    }
}

template <typename C, typename S>
auto invoke_on_remove(S& system, entity ent) -> void {
    if constexpr (has_on_remove<S, C>) {
        system.template on_remove<C>(ent);
    }
}

template <typename S>
auto invoke_shutdown(S& system) -> void {
    if constexpr (has_shutdown<S>) {
        system.shutdown();
    }
}

}  // namespace vw::ecs::detail
