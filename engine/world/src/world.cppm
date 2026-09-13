export module vw.world;

import std;

export import :scene;
export import :spatial;
export import :components;
export import :terrain;
export import :light;
export import :grid;
export import :systems;

import vw.core;
import vw.asset;
import vw.ecs;

export namespace vw::ecs {

// Порядок здесь — это порядок кадра: каждая система видит мир таким, каким его
// оставила предыдущая.
using world_systems = std::tuple< //
    hierarchy_system, character_controller_system, animation_fsm_system,
    physics_system, transform_system, model_system, spatial_system,
    light_system, socket_system, world_grid_system, animation_system
>;

inline constexpr std::size_t world_system_count = std::tuple_size_v<world_systems>;

// Имя система носит сама: таблица имён рядом с кортежем разъезжается с ним при
// первой же перестановке, и разъехавшуюся видно только по перепутанным числам.
inline constexpr auto world_system_names = []<std::size_t... Is>(
    std::index_sequence<Is...> /*unused*/
) {
    return std::array<std::string_view, sizeof...(Is)>{
        std::tuple_element_t<Is, world_systems>::system_name...
    };
}(std::make_index_sequence<world_system_count>{});

// Сколько заняла каждая система на последнем кадре, в порядке кортежа. Замер
// идёт всегда: одиннадцать чтений часов на кадр не стоят ничего, а метрика,
// которую надо включить, показывает не тот кадр, из-за которого её открыли.
struct world_update_stats {
    std::array<float32, world_system_count> ms{};
    float32 total_ms = 0.0f;
};

// Владеет реестром, системами и общими реестрами ассетов и держит их в согласии:
// добавление или удаление компонента извещает каждую систему, которой этот тип
// небезразличен.
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

    // По умолчанию — встроенный каталог: так мир поднимается в тестах и в
    // headless-сборке, где движка нет. Движок передаёт свой.
    explicit world(const block_registry& blocks = default_block_registry());
    ~world();

    world(const world&)                    = delete;
    auto operator=(const world&) -> world& = delete;
    world(world&&)                         = delete;
    auto operator=(world&&) -> world&      = delete;

    auto update(float32 delta_time) -> void;
    auto clear_changed() -> void;

    [[nodiscard]] auto blocks() const -> const block_registry& {
        return *blocks_;
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

    template <typename S>
    [[nodiscard]] auto system() -> S& {
        return std::get<S>(systems_);
    }

    template <typename S>
    [[nodiscard]] auto system() const -> const S& {
        return std::get<S>(systems_);
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
    [[nodiscard]] auto changed() -> std::unordered_set<entity>& {
        return registry_.changed<T>();
    }

    [[nodiscard]] auto destroyed() const -> const std::vector<entity>&;

    [[nodiscard]] auto get_update_stats() const -> const world_update_stats&;

private:
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
    }

    template <typename T>
    auto remove_component_(
        entity ent
    ) noexcept -> void {
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
    }

    template <typename T>
    auto batch_remove_component_(
        const std::vector<entity>& entities
    ) noexcept -> void {
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

    // Уничтожение сущности обязано известить системы о каждом компоненте, который
    // она ещё держит, а тип к тому моменту известен только по идентификатору —
    // поэтому каждый тип компонента при первом добавлении оставляет за собой
    // типизированный удалитель.
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

    // Перед системами: запекатель света снимает с реестра таблицу излучения, а
    // строится он системой сетки.
    const block_registry* blocks_;

    ecs::registry registry_;
    // Ресурсы стоят перед системами намеренно: система может держать модели, чьи
    // страницы принадлежат model_registry, поэтому реестр обязан пережить каждую
    // систему, которая у него занимала.
    resources resources_;
    systems systems_;
    std::vector<void (*)(world&, entity)> remove_hooks_;
};
}  // namespace vw::ecs
