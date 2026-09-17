module vw.world;

import std;
import vw.core;

namespace vw::ecs {

hierarchy_system::hierarchy_system(world& w)
    : world_{&w} {}

auto hierarchy_system::update(float32 /*dt*/) -> void {}

hierarchy_system::hierarchy_modifier::hierarchy_modifier(
    hierarchy_system* system,
    entity ent
)
    : system_{system}, entity_{ent} {}

auto hierarchy_system::cleanup(
    entity ent
) -> void {
    auto& reg = world_->registry();
    if (!reg.has<hierarchy_component>(ent)) {
        return;
    }

    auto& hierarchy_comp = reg.get<hierarchy_component>(ent);
    auto parent = invalid_entity;

    if (hierarchy_comp.has_parent()) {
        parent = hierarchy_comp.get_parent();
        if (reg.has<hierarchy_component>(parent)) {
            auto& parent_comp = reg.get<hierarchy_component>(parent);
            std::erase(parent_comp.children_, ent);
        }
    }

    const auto& children = hierarchy_comp.get_children();
    for (entity child : children) {
        if (reg.has<hierarchy_component>(child)) {
            auto& child_comp = reg.get<hierarchy_component>(child);
            child_comp.parent_ = invalid_entity;

            world_->system<transform_system>().modify(child).mark_world_dirty();
        }
    }
}

auto hierarchy_system::modify(entity ent) -> hierarchy_modifier {
    return hierarchy_modifier{this, ent};
}

auto hierarchy_system::get_hierarchy_depth(
    entity ent
) const -> std::size_t {
    constexpr int MAX_HIERARCHY_DEPTH = 64;

    int depth      = 0;
    entity current = ent;

    auto& reg = world_->registry();
    while (reg.has<hierarchy_component>(current)) {
        const auto& hierarchy_comp = reg.get<hierarchy_component>(current);
        if (!hierarchy_comp.has_parent()) {
            break;
        }
        current = hierarchy_comp.get_parent();
        depth++;

        if (depth >= MAX_HIERARCHY_DEPTH) {
            throw std::runtime_error("hierarchy depth is too deep");
        }
    }

    return depth;
}

auto hierarchy_system::hierarchy_modifier::set_parent(entity parent, std::size_t index)
    -> hierarchy_modifier& {
    if (!parent.is_valid()) {
        throw std::invalid_argument("parent is not valid");
    }

    if (!entity_.is_valid()) {
        throw std::invalid_argument("child is not valid");
    }

    if (parent == entity_) {
        throw std::invalid_argument("parent and child cannot be the same");
    }

    if (system_->check_hierarchy_cycle(parent, entity_)) {
        throw std::invalid_argument("setting parent to child would create a hierarchy cycle");
    }

    auto& reg = system_->world_->registry();

    // Прежний родитель отпускает узел: иначе перенос оставил бы его в двух
    // списках детей сразу, и обход дерева проходил бы его дважды.
    if (reg.has<hierarchy_component>(entity_)) {
        const auto previous = reg.get<hierarchy_component>(entity_).parent_;
        if (previous.is_valid() && reg.has<hierarchy_component>(previous)) {
            std::erase(reg.get<hierarchy_component>(previous).children_, entity_);
        }
    }

    if (reg.has<hierarchy_component>(parent)) {
        auto& children  = reg.get<hierarchy_component>(parent).children_;
        const auto slot = std::min(index, children.size());
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(slot), entity_);
    }

    if (reg.has<hierarchy_component>(entity_)) {
        auto& child_component   = reg.get<hierarchy_component>(entity_);
        child_component.parent_ = parent;

        system_->world_->system<transform_system>().modify(entity_).mark_world_dirty();
    }

    return *this;
}

auto hierarchy_system::hierarchy_modifier::remove_parent() -> hierarchy_modifier& {
    auto& reg = system_->world_->registry();
    if (reg.has<hierarchy_component>(entity_)) {
        auto& child_component = reg.get<hierarchy_component>(entity_);

        if (reg.has<hierarchy_component>(child_component.parent_)) {
            auto& parent_component =
                reg.get<hierarchy_component>(child_component.parent_);
            std::erase_if(parent_component.children_, [this](entity e) {
                return e == entity_;
            });
        }

        child_component.parent_ = invalid_entity;

        system_->world_->system<transform_system>().modify(entity_).mark_world_dirty();
    }

    return *this;
}

auto hierarchy_system::check_hierarchy_cycle(entity parent, entity child) const -> bool {
    entity current = parent;

    auto& reg = world_->registry();
    while (current.is_valid() && reg.has<hierarchy_component>(current)) {
        const auto& current_component = reg.get<hierarchy_component>(current);
        if (current_component.get_parent() == child) {
            return true;
        }
        current = current_component.get_parent();
    }

    return false;
}

}  // namespace vw::ecs
