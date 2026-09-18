export module vw.ecs:registry;

import std;

import vw.core;
import :entity;
import :pool;

namespace vw::ecs::detail {
auto next_component_id() -> uint32;

// см. docs/ENGINE.md#наборы-изменений
class change_set final {
public:
    auto insert(entity e) -> void {
        entities_.push_back(e);
        deduplicated_ = false;
    }

    auto clear() -> void {
        entities_.clear();
        deduplicated_ = true;
    }

    [[nodiscard]] auto view() -> const std::vector<entity>& {
        if (!deduplicated_) {
            std::sort(entities_.begin(), entities_.end());
            entities_.erase(std::unique(entities_.begin(), entities_.end()), entities_.end());
            deduplicated_ = true;
        }
        return entities_;
    }

private:
    std::vector<entity> entities_;
    bool deduplicated_ = true;
};
}  // namespace vw::ecs::detail

export namespace vw::ecs {

template <typename T>
auto component_id_of() -> uint32 {
    static const uint32 id = detail::next_component_id();
    return id;
}

template <typename... Cs>
class component_view;

class registry final {
public:
    [[nodiscard]] auto create() -> entity;
    [[nodiscard]] auto batch_create(uint32 count) -> std::vector<entity>;

    auto destroy(entity e) -> void;
    auto batch_destroy(const std::vector<entity>& entities) -> void;

    [[nodiscard]] auto alive(entity e) const -> bool;
    [[nodiscard]] auto alive_entities() const -> std::vector<entity>;
    [[nodiscard]] auto destroyed() const -> const std::vector<entity>&;

    auto ensure_pool(uint32 component_id, component_layout layout) -> dynamic_pool&;

    [[nodiscard]] auto try_pool(uint32 component_id) -> pool_base*;
    [[nodiscard]] auto try_pool(uint32 component_id) const -> const pool_base*;
    [[nodiscard]] auto pool_count() const -> uint32;

    auto remove_all(entity e) -> void;
    auto collect_components(entity e, std::vector<uint32>& ids_out) const -> void;

    auto add_change_dep(uint32 component_id, uint32 dependent_id) -> void;
    auto notify_changed(uint32 component_id, entity e) -> void;
    auto request_change(uint32 component_id, entity e) -> void;
    [[nodiscard]] auto changed_set(uint32 component_id) -> const std::vector<entity>&;
    [[nodiscard]] auto requested_set(uint32 component_id) -> const std::vector<entity>&;
    auto clear_requested(uint32 component_id) -> void;
    auto clear_changed() -> void;

    template <typename T>
    auto pool_of() -> component_pool<T>& {
        auto& slot = pool_slot_(component_id_of<T>());
        if (slot == nullptr) {
            slot = std::make_unique<component_pool<T>>();
        }
        return static_cast<component_pool<T>&>(*slot);
    }

    template <typename T>
    [[nodiscard]] auto try_pool_of() -> component_pool<T>* {
        return static_cast<component_pool<T>*>(try_pool(component_id_of<T>()));
    }

    template <typename T>
    [[nodiscard]] auto try_pool_of() const -> const component_pool<T>* {
        return static_cast<const component_pool<T>*>(try_pool(component_id_of<T>()));
    }

    template <typename T, typename... Args>
    auto emplace(entity e, Args&&... args) -> T& {
        return pool_of<T>().emplace(e, std::forward<Args>(args)...);
    }

    template <typename T>
    auto add(entity e, T&& value = {}) -> void {
        pool_of<std::remove_cvref_t<T>>().emplace(e, std::forward<T>(value));
    }

    template <typename T>
    auto batch_add(const std::vector<entity>& entities, const T& value = {}) -> void {
        auto& pool = pool_of<T>();
        for (auto e : entities) {
            pool.emplace(e, value);
        }
    }

    template <typename T>
    [[nodiscard]] auto has(entity e) const -> bool {
        if (!alive(e)) {
            return false;
        }
        const auto* pool = try_pool_of<T>();
        return pool != nullptr && pool->has(e);
    }

    template <typename T>
    [[nodiscard]] auto get(entity e) -> T& {
        return *try_pool_of<T>()->get(e);
    }

    template <typename T>
    [[nodiscard]] auto get(entity e) const -> const T& {
        return *try_pool_of<T>()->get(e);
    }

    template <typename T>
    auto remove(entity e) -> void {
        if (auto* pool = try_pool_of<T>()) {
            pool->remove(e);
        }
    }

    template <typename T>
    auto batch_remove(const std::vector<entity>& entities) -> void {
        if (auto* pool = try_pool_of<T>()) {
            pool->batch_remove(entities);
        }
    }

    template <typename... Cs>
    [[nodiscard]] auto view() -> component_view<Cs...> {
        return component_view<Cs...>(*this);
    }

    template <typename... Cs, typename Fn>
    auto for_each(Fn&& fn) -> void {
        component_view<Cs...>(*this).for_each(std::forward<Fn>(fn));
    }

    template <typename T>
    auto request_change(entity e) -> void {
        request_change(component_id_of<T>(), e);
    }

    template <typename T>
    [[nodiscard]] auto requested() -> const std::vector<entity>& {
        return requested_set(component_id_of<T>());
    }

    template <typename T>
    auto clear_requested() -> void {
        clear_requested(component_id_of<T>());
    }

    template <typename T>
    auto notify_changed(entity e) -> void {
        notify_changed(component_id_of<T>(), e);
    }

    template <typename T>
    [[nodiscard]] auto changed() -> const std::vector<entity>& {
        return changed_set(component_id_of<T>());
    }

private:
    auto ensure_id_slot_(uint32 component_id) -> void;
    auto pool_slot_(uint32 component_id) -> std::unique_ptr<pool_base>&;

    entity_pool entity_pool_;
    std::vector<std::unique_ptr<pool_base>> pools_;
    std::vector<std::vector<uint32>> change_deps_;
    std::vector<detail::change_set> request_sets_;
    std::vector<detail::change_set> changed_sets_;
    std::vector<entity> destroyed_set_;
};

template <typename... Cs>
class component_view {
    template <typename C>
    struct cursor {
        C* dense                = nullptr;
        const uint32* sparse    = nullptr;
        std::size_t sparse_size = 0;
        const entity* owners    = nullptr;

        [[nodiscard]] auto slot_of(entity e) const -> uint32 {
            if (e.index >= sparse_size) {
                return entity::invalid_index;
            }
            const uint32 slot = sparse[e.index];
            return slot != entity::invalid_index && owners[slot] == e ? slot
                                                                      : entity::invalid_index;
        }

        [[nodiscard]] auto locate(entity e, uint32& slot_out) const -> bool {
            slot_out = slot_of(e);
            return slot_out != entity::invalid_index;
        }
    };

public:
    explicit component_view(registry& reg) : registry_{&reg} {
        pick_entities_();
    }

    struct iterator {
        const component_view* view = nullptr;
        std::size_t index          = 0;
        std::array<uint32, sizeof...(Cs)> slots{};

        [[nodiscard]] auto operator==(const iterator& rhs) const -> bool {
            return index == rhs.index;
        }

        [[nodiscard]] auto operator!=(const iterator& rhs) const -> bool {
            return !(*this == rhs);
        }

        void operator++() {
            ++index;
            skip_missing();
        }

        [[nodiscard]] auto operator*() const -> std::tuple<entity, Cs&...> {
            return deref_(std::index_sequence_for<Cs...>{});
        }

        auto skip_missing() -> void {
            while (index < view->count_ &&
                   !locate_all_(view->owners_[index], std::index_sequence_for<Cs...>{})) {
                ++index;
            }
        }

    private:
        template <std::size_t... Is>
        [[nodiscard]] auto locate_all_(entity e, std::index_sequence<Is...>) -> bool {
            return (std::get<Is>(view->cursors_).locate(e, slots[Is]) && ...);
        }

        template <std::size_t... Is>
        [[nodiscard]] auto deref_(std::index_sequence<Is...>) const
            -> std::tuple<entity, Cs&...> {
            return std::tuple<entity, Cs&...>{
                view->owners_[index], std::get<Is>(view->cursors_).dense[slots[Is]]...};
        }
    };

    [[nodiscard]] auto begin() const -> iterator {
        iterator it{this, 0};
        it.skip_missing();
        return it;
    }

    [[nodiscard]] auto end() const -> iterator {
        return iterator{this, count_};
    }

    template <typename Fn>
    auto for_each(Fn&& fn) const -> void {
        for_each_(std::forward<Fn>(fn), std::index_sequence_for<Cs...>{});
    }

private:
    template <typename Fn, std::size_t... Is>
    auto for_each_(Fn&& fn, std::index_sequence<Is...>) const -> void {
        for (std::size_t i = 0; i < count_; ++i) {
            const entity e = owners_[i];
            const std::array<uint32, sizeof...(Cs)> slots{
                std::get<Is>(cursors_).slot_of(e)...};
            if (((slots[Is] != entity::invalid_index) && ...)) {
                fn(e, std::get<Is>(cursors_).dense[slots[Is]]...);
            }
        }
    }

    template <typename C>
    static auto make_cursor(component_pool<C>& pool) -> cursor<C> {
        return {
            .dense       = pool.data(),
            .sparse      = pool.sparse().data(),
            .sparse_size = pool.sparse().size(),
            .owners      = pool.entities().data(),
        };
    }

    auto pick_entities_() -> void {
        if constexpr (sizeof...(Cs) == 0) {
            return;
        } else {
            const std::tuple<component_pool<Cs>*...> pools{
                registry_->template try_pool_of<Cs>()...};

            const bool complete =
                std::apply([](auto*... pool) -> bool { return ((pool != nullptr) && ...); }, pools);
            if (!complete) {
                return;
            }

            cursors_ = std::apply(
                [](auto*... pool) -> std::tuple<cursor<Cs>...> {
                    return {make_cursor(*pool)...};
                },
                pools);

            const auto sized = std::apply(
                [](auto*... pool) -> std::array<const pool_base*, sizeof...(Cs)> {
                    return {pool...};
                },
                pools);

            const auto* smallest = *std::ranges::min_element(
                sized,
                [](const pool_base* a, const pool_base* b) -> bool { return a->size() < b->size(); });
            owners_ = smallest->entities().data();
            count_  = smallest->size();
        }
    }

    registry* registry_ = nullptr;
    std::tuple<cursor<Cs>...> cursors_;
    const entity* owners_ = nullptr;
    std::size_t count_    = 0;
};

}  // namespace vw::ecs
