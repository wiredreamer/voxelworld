module vw.world;

import std;
import vw.core;

namespace vw::ecs {
namespace {

template <typename Tuple, std::size_t... Is>
auto make_systems(world& w, std::index_sequence<Is...> /*unused*/) -> Tuple {
    return Tuple{std::tuple_element_t<Is, Tuple>(w)...};
}

}  // namespace

world::world(
    const block_registry& blocks
)
    : blocks_{&blocks}
    , systems_{make_systems<systems>(
          *this, std::make_index_sequence<std::tuple_size_v<systems>>{})} {
    const uint32 transform_id = component_id_of<transform_component>();
    const uint32 model_id     = component_id_of<model_component>();

    registry_.add_change_dep(transform_id, component_id_of<spatial_component>());
    registry_.add_change_dep(transform_id, component_id_of<world_view_component>());
    registry_.add_change_dep(transform_id, component_id_of<light_component>());
    registry_.add_change_dep(model_id, component_id_of<spatial_component>());
}

world::~world() {
    std::apply([](auto&... s) { (detail::invoke_shutdown(s), ...); }, systems_);

    for (auto ent : registry_.alive_entities()) {
        destroy(ent);
    }
}

auto world::update(float32 delta_time) -> void {
    std::size_t index = 0;
    std::apply(
        [&](auto&... s) {
            ((update_stats_.ms[index++] = measure_ms([&] { s.update(delta_time); })), ...);
        },
        systems_
    );

    update_stats_.total_ms =
        std::accumulate(update_stats_.ms.begin(), update_stats_.ms.end(), 0.0f);
}

auto world::clear_changed() -> void {
    registry_.clear_changed();
}

auto world::create() -> modifier {
    return modifier(*this, registry_.create());
}

auto world::modify(entity ent) -> modifier {
    return modifier(*this, ent);
}

auto world::destroy(entity ent) noexcept -> void {
    detach_components_(ent);
    registry_.destroy(ent);
}

auto world::batch_create(uint32 count) -> batch_modifier {
    return batch_modifier(*this, registry_.batch_create(count));
}

auto world::batch_modify(std::vector<entity> entities) -> batch_modifier {
    return batch_modifier(*this, std::move(entities));
}

auto world::batch_destroy(const std::vector<entity>& entities) noexcept -> void {
    for (auto ent : entities) {
        detach_components_(ent);
    }
    registry_.batch_destroy(entities);
}

auto world::registry() -> ecs::registry& {
    return registry_;
}

auto world::destroyed() const -> const std::vector<entity>& {
    return registry_.destroyed();
}

auto world::get_update_stats() const -> const world_update_stats& {
    return update_stats_;
}

auto world::detach_components_(entity ent) noexcept -> void {
    for (uint32 id = 0; id < registry_.pool_count(); ++id) {
        const auto* pool = registry_.try_pool(id);
        if (pool == nullptr || !pool->has(ent)) {
            continue;
        }
        if (id < remove_hooks_.size() && remove_hooks_[id] != nullptr) {
            remove_hooks_[id](*this, ent);
        }
    }
}

}  // namespace vw::ecs