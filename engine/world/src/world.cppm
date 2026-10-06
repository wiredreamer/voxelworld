export module vw.world;

import std;

export import :scene;
export import :spatial;
export import :components;
export import :terrain;
export import :light;
export import :grid;
export import :flora.grass;
export import :systems;

import vw.core;
import vw.asset;
import vw.ecs;

export namespace vw::ecs {

using world_systems = std::tuple< //
    hierarchy_system, character_controller_system, animation_fsm_system,
    physics_system, transform_system, model_system, spatial_system, lod_system,
    light_system, socket_system, world_grid_system, animation_system,
    variant_system, structure_system
>;

inline constexpr std::size_t world_system_count = std::tuple_size_v<world_systems>;

inline constexpr auto world_system_names = []<std::size_t... Is>(
    std::index_sequence<Is...>
) {
    return std::array<std::string_view, sizeof...(Is)>{
        std::tuple_element_t<Is, world_systems>::system_name...
    };
}(std::make_index_sequence<world_system_count>{});

struct world_update_stats {
    std::array<float32, world_system_count> ms{};
    float32 total_ms = 0.0f;
};

class world final {
public:
    using systems = world_systems;

    using resources = std::tuple<asset::model_registry, asset::animation_clip_registry>;

    class modifier {
    public:
        modifier(
            world& w, entity ent
        )
            : world_{&w}, ent_{ent} {}

        template <typename C>
        auto with(
            C&& value = {}
        ) -> modifier& {
            world_->add_component_<C>(ent_, std::forward<C>(value));
            return *this;
        }

        template <typename C>
        auto without() -> modifier& {
            world_->remove_component_<C>(ent_);
            return *this;
        }

        [[nodiscard]] auto get_entity() const -> entity {
            return ent_;
        }

    private:
        world* world_;
        entity ent_;
    };

    class batch_modifier {
    public:
        batch_modifier(
            world& w, std::vector<entity> entities
        )
            : world_{&w}, entities_{std::move(entities)} {}

        template <typename C>
        auto with(
            const C& value = {}
        ) -> batch_modifier& {
            world_->batch_add_component_<C>(entities_, value);
            return *this;
        }

        template <typename C>
        auto without() -> batch_modifier& {
            world_->batch_remove_component_<C>(entities_);
            return *this;
        }

        [[nodiscard]] auto get_entities() const -> const std::vector<entity>& {
            return entities_;
        }

        [[nodiscard]] auto release_entities() -> std::vector<entity> {
            return std::move(entities_);
        }

    private:
        world* world_;
        std::vector<entity> entities_;
    };

    explicit world(const voxel_registry& voxel_types = default_voxel_registry());
    ~world();

    world(const world&)                    = delete;
    auto operator=(const world&) -> world& = delete;
    world(world&&)                         = delete;
    auto operator=(world&&) -> world&      = delete;

    auto update(float32 delta_time) -> void;
    auto clear_changed() -> void;

    [[nodiscard]] auto voxel_types() const -> const voxel_registry& {
        return *voxel_types_;
    }

    [[nodiscard]] auto create() -> modifier;
    [[nodiscard]] auto modify(entity ent) -> modifier;
    auto destroy(entity ent) noexcept -> void;

    [[nodiscard]] auto batch_create(uint32 count) -> batch_modifier;
    [[nodiscard]] auto batch_modify(std::vector<entity> entities) -> batch_modifier;
    auto batch_destroy(const std::vector<entity>& entities) noexcept -> void;

    template <typename T>
    [[nodiscard]] auto has(
        entity ent
    ) const -> bool {
        return registry_.has<T>(ent);
    }

    template <typename T>
    [[nodiscard]] auto try_get(
        entity ent
    ) -> T* {
        return registry_.try_get<T>(ent);
    }

    template <typename T>
    [[nodiscard]] auto try_get(
        entity ent
    ) const -> const T* {
        return registry_.try_get<T>(ent);
    }

    template <typename T>
    [[nodiscard]] auto get(
        entity ent
    ) -> T& {
        return registry_.get<T>(ent);
    }

    template <typename T>
    [[nodiscard]] auto get(
        entity ent
    ) const -> const T& {
        return registry_.get<T>(ent);
    }

    template <typename... Cs>
    [[nodiscard]] auto view() -> component_view<Cs...> {
        return registry_.view<Cs...>();
    }

    template <typename... Cs, typename Fn>
    auto for_each(
        Fn&& fn
    ) -> void {
        registry_.for_each<Cs...>(std::forward<Fn>(fn));
    }

    template <typename S, typename... Args>
        requires extension_system<S> && (!detail::tuple_has<S, world_systems>) &&
                 std::constructible_from<S, world&, Args...>
    auto add_system(
        tick_stage stage, Args&&... args
    ) -> S& {
        auto owned = std::make_unique<S>(*this, std::forward<Args>(args)...);
        S& added   = *owned;

        install_extension_(
            system_id_of<S>(), stage,
            detail::extension_slot{
                .instance = owned.get(),
                .update =
                    +[](void* self, float32 dt) { static_cast<S*>(self)->update(dt); },
                .shutdown = shutdown_thunk_<S>(),
                .destroy  = +[](void* self) { delete static_cast<S*>(self); },
                .name     = S::system_name,
            }
        );
        static_cast<void>(owned.release());

        if constexpr (has_observed_components<S>) {
            observe_(added, typename S::observed_components{});
        }
        return added;
    }

    template <typename S>
    [[nodiscard]] auto system() -> S& {
        if constexpr (detail::tuple_has<S, world_systems>) {
            return std::get<S>(systems_);
        } else {
            return *static_cast<S*>(extension_instance_(system_id_of<S>()));
        }
    }

    template <typename S>
    [[nodiscard]] auto system() const -> const S& {
        if constexpr (detail::tuple_has<S, world_systems>) {
            return std::get<S>(systems_);
        } else {
            return *static_cast<const S*>(extension_instance_(system_id_of<S>()));
        }
    }

    template <typename S>
    [[nodiscard]] auto try_system() -> S* {
        if constexpr (detail::tuple_has<S, world_systems>) {
            return std::addressof(std::get<S>(systems_));
        } else {
            return static_cast<S*>(extension_instance_(system_id_of<S>()));
        }
    }

    template <typename S>
    [[nodiscard]] auto try_system() const -> const S* {
        if constexpr (detail::tuple_has<S, world_systems>) {
            return std::addressof(std::get<S>(systems_));
        } else {
            return static_cast<const S*>(extension_instance_(system_id_of<S>()));
        }
    }

    template <typename R>
    [[nodiscard]] auto resource() -> R& {
        return std::get<R>(resources_);
    }

    template <typename R>
    [[nodiscard]] auto resource() const -> const R& {
        return std::get<R>(resources_);
    }

    [[nodiscard]] auto registry() -> ecs::registry&;

    template <typename T>
    [[nodiscard]] auto changed() -> const std::vector<entity>& {
        return registry_.changed<T>();
    }

    [[nodiscard]] auto destroyed() const -> const std::vector<entity>&;

    [[nodiscard]] auto get_update_stats() const -> const world_update_stats&;
    [[nodiscard]] auto get_extension_timings() const -> std::span<const extension_timing>;

private:
    template <typename S>
    [[nodiscard]] static auto shutdown_thunk_() -> void (*)(void*) {
        if constexpr (has_shutdown<S>) {
            return +[](void* self) { static_cast<S*>(self)->shutdown(); };
        } else {
            return nullptr;
        }
    }

    template <typename S, typename... Cs>
    auto observe_(
        S& observer, component_list<Cs...>
    ) -> void {
        (observe_component_<S, Cs>(observer), ...);
    }

    template <typename S, typename C>
    auto observe_component_(
        S& observer
    ) -> void {
        static_assert(
            has_on_add<S, C> || has_on_remove<S, C>,
            "an observed component needs on_add<C> or on_remove<C> in the system"
        );

        if constexpr (has_on_add<S, C>) {
            add_extension_hook_(
                extension_add_hooks_, component_id_of<C>(),
                detail::component_hook{
                    .system = std::addressof(observer),
                    .invoke = +[](void* self, entity ent) {
                        static_cast<S*>(self)->template on_add<C>(ent);
                    },
                }
            );
        }
        if constexpr (has_on_remove<S, C>) {
            add_extension_hook_(
                extension_remove_hooks_, component_id_of<C>(),
                detail::component_hook{
                    .system = std::addressof(observer),
                    .invoke = +[](void* self, entity ent) {
                        static_cast<S*>(self)->template on_remove<C>(ent);
                    },
                }
            );
        }
    }

    [[nodiscard]] auto extension_instance_(
        uint32 system_id
    ) const -> void* {
        return system_id < extensions_.size() ? extensions_[system_id].instance : nullptr;
    }

    static auto invoke_extension_hooks_(
        const detail::component_hook_table& hooks, uint32 component_id, entity ent
    ) -> void {
        if (component_id >= hooks.size()) {
            return;
        }
        for (const auto& hook : hooks[component_id]) {
            hook.invoke(hook.system, ent);
        }
    }

    auto install_extension_(
        uint32 system_id, tick_stage stage, const detail::extension_slot& slot
    ) -> void;
    auto add_extension_hook_(
        detail::component_hook_table& hooks, uint32 component_id, detail::component_hook hook
    ) -> void;
    auto run_stage_(tick_stage stage, float32 delta_time, std::size_t& timing_index) -> void;
    auto report_late_changes_() -> void;
    [[nodiscard]] auto change_actor_name_(uint32 actor) const -> std::string_view;

    template <typename T>
    auto add_component_(
        entity ent, T&& value = {}
    ) -> void {
        using C = std::remove_cvref_t<T>;
        remember_remove_hook_<C>();
        registry_.add<C>(ent, std::forward<T>(value));

        std::apply(
            [&](auto&... systems) { (detail::invoke_on_add<C>(systems, ent), ...); },
            systems_
        );
        invoke_extension_hooks_(extension_add_hooks_, component_id_of<C>(), ent);
    }

    template <typename T>
    auto remove_component_(
        entity ent
    ) noexcept -> void {
        invoke_extension_hooks_(extension_remove_hooks_, component_id_of<T>(), ent);
        std::apply(
            [&](auto&... systems) { (detail::invoke_on_remove<T>(systems, ent), ...); },
            systems_
        );
        registry_.remove<T>(ent);
    }

    template <typename T>
    auto batch_add_component_(
        const std::vector<entity>& entities, const T& value = {}
    ) -> void {
        remember_remove_hook_<T>();
        registry_.batch_add<T>(entities, value);

        std::apply(
            [&](auto&... systems) {
                for (auto ent : entities) {
                    (detail::invoke_on_add<T>(systems, ent), ...);
                }
            },
            systems_
        );
        for (auto ent : entities) {
            invoke_extension_hooks_(extension_add_hooks_, component_id_of<T>(), ent);
        }
    }

    template <typename T>
    auto batch_remove_component_(
        const std::vector<entity>& entities
    ) noexcept -> void {
        for (auto ent : entities) {
            invoke_extension_hooks_(extension_remove_hooks_, component_id_of<T>(), ent);
        }
        std::apply(
            [&](auto&... systems) {
                for (auto ent : entities) {
                    (detail::invoke_on_remove<T>(systems, ent), ...);
                }
            },
            systems_
        );
        registry_.batch_remove<T>(entities);
    }

    template <typename T>
    auto remember_remove_hook_() -> void {
        const uint32 id = component_id_of<T>();
        if (id >= remove_hooks_.size()) {
            remove_hooks_.resize(id + 1, nullptr);
        }
        if (remove_hooks_[id] == nullptr) {
            remove_hooks_[id] = +[](world& w, entity ent) { w.remove_component_<T>(ent); };
        }
    }

    auto detach_components_(entity ent) noexcept -> void;

    world_update_stats update_stats_;

    const voxel_registry* voxel_types_;

    ecs::registry registry_;
    resources resources_;
    systems systems_;
    std::vector<void (*)(world&, entity)> remove_hooks_;

    std::vector<detail::extension_slot> extensions_;
    std::vector<uint32> extension_order_;
    std::array<std::vector<uint32>, tick_stage_count> stage_order_;
    std::vector<extension_timing> extension_timings_;
    detail::component_hook_table extension_add_hooks_;
    detail::component_hook_table extension_remove_hooks_;
    std::size_t reported_late_changes_ = 0;
};
}  // namespace vw::ecs
