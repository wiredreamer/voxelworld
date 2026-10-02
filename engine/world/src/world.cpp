module vw.world;

import std;
import vw.core;

namespace vw::ecs {
namespace detail {

auto next_system_id() -> uint32 {
    static std::atomic<uint32> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

namespace {

constexpr log::log_category lc_{"world"};

template <typename Tuple, std::size_t... Is>
auto make_systems(world& w, std::index_sequence<Is...>) -> Tuple {
    return Tuple{std::tuple_element_t<Is, Tuple>(w)...};
}

auto extension_actor(uint32 system_id) -> uint32 {
    return static_cast<uint32>(world_system_count) + system_id;
}

}  // namespace

world::world(
    const voxel_registry& voxel_types
)
    : voxel_types_{&voxel_types}
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
    for (const uint32 system_id : extension_order_ | std::views::reverse) {
        const auto& slot = extensions_[system_id];
        if (slot.shutdown != nullptr) {
            slot.shutdown(slot.instance);
        }
    }
    std::apply([](auto&... s) { (detail::invoke_shutdown(s), ...); }, systems_);

    for (auto ent : registry_.alive_entities()) {
        destroy(ent);
    }

    for (const uint32 system_id : extension_order_ | std::views::reverse) {
        auto& slot = extensions_[system_id];
        slot.destroy(slot.instance);
        slot.instance = nullptr;
    }
}

auto world::update(float32 delta_time) -> void {
    registry_.open_change_tick();

    std::size_t timing_index = 0;
    run_stage_(tick_stage::before_engine, delta_time, timing_index);

    std::size_t index = 0;
    const auto tick   = [&](auto& s) {
        registry_.set_change_actor(static_cast<uint32>(index));
        update_stats_.ms[index] = measure_ms([&] { s.update(delta_time); });
        ++index;
    };
    std::apply([&](auto&... s) { (tick(s), ...); }, systems_);

    run_stage_(tick_stage::after_engine, delta_time, timing_index);
    registry_.set_change_actor(no_change_actor);

    update_stats_.total_ms =
        std::accumulate(update_stats_.ms.begin(), update_stats_.ms.end(), 0.0f);
    for (const auto& timing : extension_timings_) {
        update_stats_.total_ms += timing.ms;
    }

    report_late_changes_();
}

auto world::run_stage_(
    tick_stage stage, float32 delta_time, std::size_t& timing_index
) -> void {
    for (const uint32 system_id : stage_order_[std::to_underlying(stage)]) {
        const auto& slot = extensions_[system_id];
        registry_.set_change_actor(extension_actor(system_id));
        extension_timings_[timing_index].ms =
            measure_ms([&] { slot.update(slot.instance, delta_time); });
        ++timing_index;
    }
}

auto world::report_late_changes_() -> void {
    const auto late = registry_.late_changes();
    for (; reported_late_changes_ < late.size(); ++reported_late_changes_) {
        const auto& change = late[reported_late_changes_];
        log::warn(
            lc_,
            "system '{}' reads the changed set of component {} before system '{}' writes it",
            change_actor_name_(change.reader), change.component_id,
            change_actor_name_(change.writer)
        );
    }
}

auto world::change_actor_name_(uint32 actor) const -> std::string_view {
    if (actor < world_system_count) {
        return world_system_names[actor];
    }
    const std::size_t system_id = actor - world_system_count;
    return system_id < extensions_.size() ? extensions_[system_id].name : std::string_view{};
}

auto world::install_extension_(
    uint32 system_id, tick_stage stage, const detail::extension_slot& slot
) -> void {
    if (system_id >= extensions_.size()) {
        extensions_.resize(static_cast<std::size_t>(system_id) + 1);
    }
    if (extensions_[system_id].instance != nullptr) {
        throw std::logic_error("the system is already added to this world");
    }

    extensions_[system_id] = slot;
    extension_order_.push_back(system_id);
    stage_order_[std::to_underlying(stage)].push_back(system_id);

    extension_timings_.clear();
    for (std::size_t i = 0; i < tick_stage_count; ++i) {
        for (const uint32 id : stage_order_[i]) {
            extension_timings_.push_back(
                {.name = extensions_[id].name, .stage = static_cast<tick_stage>(i)}
            );
        }
    }
}

auto world::add_extension_hook_(
    detail::component_hook_table& hooks, uint32 component_id, detail::component_hook hook
) -> void {
    const auto* pool = registry_.try_pool(component_id);
    if (pool != nullptr && pool->size() > 0) {
        throw std::logic_error(
            "a system observing a component must be added before the component is in use"
        );
    }

    if (component_id >= hooks.size()) {
        hooks.resize(static_cast<std::size_t>(component_id) + 1);
    }
    hooks[component_id].push_back(hook);
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

auto world::get_extension_timings() const -> std::span<const extension_timing> {
    return extension_timings_;
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
