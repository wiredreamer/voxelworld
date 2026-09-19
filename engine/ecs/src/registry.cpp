module vw.ecs;

import std;
import vw.core;

namespace vw::ecs::detail {

auto next_component_id() -> uint32 {
    static std::atomic<uint32> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace vw::ecs::detail

namespace vw::ecs {

auto registry::create() -> entity {
    return entity_pool_.create();
}

auto registry::batch_create(uint32 count) -> std::vector<entity> {
    return entity_pool_.batch_create(count);
}

auto registry::destroy(entity e) -> void {
    remove_all(e);
    destroyed_set_.push_back(e);
    entity_pool_.destroy(e);
}

auto registry::batch_destroy(const std::vector<entity>& entities) -> void {
    for (auto e : entities) {
        remove_all(e);
    }
    destroyed_set_.insert(destroyed_set_.end(), entities.begin(), entities.end());
    entity_pool_.batch_destroy(entities);
}

auto registry::alive(entity e) const -> bool {
    return entity_pool_.has(e);
}

auto registry::alive_entities() const -> std::vector<entity> {
    return entity_pool_.alive_entities();
}

auto registry::destroyed() const -> const std::vector<entity>& {
    return destroyed_set_;
}

auto registry::ensure_pool(uint32 component_id, component_layout layout) -> dynamic_pool& {
    auto& slot = pool_slot_(component_id);
    if (slot == nullptr) {
        slot = std::make_unique<dynamic_pool>(layout);
    }
    return static_cast<dynamic_pool&>(*slot);
}

auto registry::pool_slot_(uint32 component_id) -> std::unique_ptr<pool_base>& {
    ensure_id_slot_(component_id);
    return pools_[component_id];
}

auto registry::try_pool(uint32 component_id) -> pool_base* {
    return component_id < pools_.size() ? pools_[component_id].get() : nullptr;
}

auto registry::try_pool(uint32 component_id) const -> const pool_base* {
    return component_id < pools_.size() ? pools_[component_id].get() : nullptr;
}

auto registry::pool_count() const -> uint32 {
    return static_cast<uint32>(pools_.size());
}

auto registry::remove_all(entity e) -> void {
    for (auto& pool : pools_) {
        if (pool != nullptr) {
            pool->remove(e);
        }
    }
}

auto registry::collect_components(entity e, std::vector<uint32>& ids_out) const -> void {
    for (uint32 id = 0; id < pools_.size(); ++id) {
        if (pools_[id] != nullptr && pools_[id]->has(e)) {
            ids_out.push_back(id);
        }
    }
}

auto registry::add_change_dep(uint32 component_id, uint32 dependent_id) -> void {
    ensure_id_slot_(component_id);
    ensure_id_slot_(dependent_id);
    auto& deps = change_deps_[component_id];
    if (std::ranges::find(deps, dependent_id) == deps.end()) {
        deps.push_back(dependent_id);
    }
}

auto registry::notify_changed(uint32 component_id, entity e) -> void {
    ensure_id_slot_(component_id);
    changed_sets_[component_id].insert(e);

    for (uint32 dep : change_deps_[component_id]) {
        const auto* pool = try_pool(dep);
        if (pool != nullptr && pool->has(e)) {
            request_sets_[dep].insert(e);
        }
    }
}

auto registry::request_change(uint32 component_id, entity e) -> void {
    ensure_id_slot_(component_id);
    request_sets_[component_id].insert(e);
}

auto registry::changed_set(uint32 component_id) -> const std::vector<entity>& {
    ensure_id_slot_(component_id);
    return changed_sets_[component_id].view();
}

auto registry::requested_set(uint32 component_id) -> const std::vector<entity>& {
    ensure_id_slot_(component_id);
    return request_sets_[component_id].view();
}

auto registry::clear_requested(uint32 component_id) -> void {
    ensure_id_slot_(component_id);
    request_sets_[component_id].clear();
}

auto registry::memory_bytes() const -> std::size_t {
    std::size_t total = 0;
    for (const auto& pool : pools_) {
        if (pool != nullptr) {
            total += pool->memory_bytes();
        }
    }
    return total;
}

auto registry::clear_changed() -> void {
    for (auto& set : changed_sets_) {
        set.clear();
    }
    destroyed_set_.clear();
}

auto registry::ensure_id_slot_(uint32 component_id) -> void {
    const std::size_t needed = static_cast<std::size_t>(component_id) + 1;
    if (needed > pools_.size()) {
        pools_.resize(needed);
        change_deps_.resize(needed);
        request_sets_.resize(needed);
        changed_sets_.resize(needed);
    }
}

}  // namespace vw::ecs
