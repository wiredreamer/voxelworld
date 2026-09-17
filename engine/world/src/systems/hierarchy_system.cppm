export module vw.world:systems.hierarchy;

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

class hierarchy_system final {
public:
    static constexpr std::string_view system_name = "hierarchy";

    explicit hierarchy_system(world& w);

    auto update(float32 dt) -> void;

    class hierarchy_modifier {
    public:
        // Место среди детей нового родителя: по умолчанию — в конец. Порядок детей
        // и есть порядок узлов в файле и в дереве редактора, поэтому перенос и
        // его отмена обязаны уметь ставить узел туда, где он стоял.
        auto set_parent(
            entity parent, std::size_t index = std::numeric_limits<std::size_t>::max()
        ) -> hierarchy_modifier&;
        auto remove_parent() -> hierarchy_modifier&;

    private:
        friend class hierarchy_system;
        hierarchy_modifier(hierarchy_system* system, entity ent);

        hierarchy_system* system_;
        entity entity_;
    };

    auto cleanup(entity ent) -> void;

    template <typename C>
        requires std::same_as<C, hierarchy_component>
    auto on_remove(entity e) -> void {
        cleanup(e);
    }

    [[nodiscard]] auto modify(entity ent) -> hierarchy_modifier;

    [[nodiscard]] auto get_hierarchy_depth(entity ent) const -> std::size_t;

private:
    [[nodiscard]] auto check_hierarchy_cycle(entity parent, entity child) const -> bool;

    world* world_;
};

}  // namespace vw::ecs
