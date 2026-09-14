module vw.world;

import std;
import vw.core;

namespace vw::ecs {

transform_system::transform_system(world& w)
    : world_(&w) {}

template <typename C>
    requires (std::same_as<C, transform_component> || std::same_as<C, spatial_component>)
auto transform_system::on_add(entity e) -> void {
    world_->registry().request_change<transform_component>(e);
}

transform_system::transform_modifier::transform_modifier(
    transform_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto transform_system::modify(
    entity ent
) -> transform_modifier {
    return transform_modifier(this, ent);
}

auto transform_system::transform_modifier::set_position(
    const vec3f& position
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.set_position(position);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::set_rotation(
    const quat& rotation
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.set_rotation(rotation);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::set_rotation_euler(
    const vec3f& euler
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.set_rotation_euler(euler);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::set_scale(
    const vec3f& scale
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.set_scale(scale);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::translate(
    const vec3f& offset
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.translate(offset);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::rotate(
    const vec3f& angles
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.rotate(angles);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::scale(
    const vec3f& factor
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp = reg.get<transform_component>(entity_);
    transform_comp.transform_.scale(factor);
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::mark_world_dirty() -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp        = reg.get<transform_component>(entity_);
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::set_transform(
    const transform& transform
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp        = reg.get<transform_component>(entity_);
    transform_comp.transform_   = transform;
    transform_comp.local_dirty_ = true;
    transform_comp.world_dirty_ = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::transform_modifier::set_transform_with_matrix(
    const transform& transform,
    const mat4f& local_matrix
) -> transform_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<transform_component>(entity_)) {
        return *this;
    }

    auto& transform_comp          = reg.get<transform_component>(entity_);
    transform_comp.transform_     = transform;
    transform_comp.local_matrix_  = local_matrix;
    transform_comp.local_dirty_   = false;
    transform_comp.world_dirty_   = true;

    reg.request_change<transform_component>(entity_);
    system_->mark_children_world_dirty(entity_);

    return *this;
}

auto transform_system::update(float32 /*dt*/) -> void {
    auto& reg       = world_->registry();
    auto& requested = reg.requested<transform_component>();
    if (requested.empty()) {
        return;
    }

    // Глубина считается подъёмом по цепочке родителей с поиском компонента на
    // каждом уровне, поэтому вычисляется по разу на сущность, а не дважды на
    // сравнение.
    auto& hierarchy = world_->system<hierarchy_system>();

    sorted_entities_.clear();
    sorted_entities_.reserve(requested.size());
    for (entity ent : requested) {
        sorted_entities_.emplace_back(hierarchy.get_hierarchy_depth(ent), ent);
    }

    std::ranges::sort(sorted_entities_, {}, &std::pair<std::size_t, entity>::first);

    for (const auto& [depth, ent] : sorted_entities_) {
        if (!reg.has<transform_component>(ent)) {
            continue;
        }

        auto& transform_comp = reg.get<transform_component>(ent);

        if (transform_comp.local_dirty_) {
            transform_comp.local_matrix_ = transform_comp.transform_.calc_matrix();
            transform_comp.local_dirty_  = false;
        }

        if (transform_comp.world_dirty_) {
            update_entity_world_matrix(ent, transform_comp);
            transform_comp.world_dirty_ = false;
        }

        reg.notify_changed<transform_component>(ent);
    }

    sorted_entities_.clear();
    reg.clear_requested<transform_component>();
}

auto transform_system::mark_children_world_dirty(
    entity ent
) -> void {
    auto& reg = world_->registry();
    if (!reg.has<hierarchy_component>(ent)) {
        return;
    }

    const auto& hierarchy_comp = reg.get<hierarchy_component>(ent);
    const auto& children       = hierarchy_comp.get_children();

    for (entity child : children) {
        if (reg.has<transform_component>(child)) {
            auto& child_transform        = reg.get<transform_component>(child);
            child_transform.world_dirty_ = true;
            reg.request_change<transform_component>(child);
        }
        mark_children_world_dirty(child);
    }
}
auto transform_system::update_entity_world_matrix(
    entity ent, const transform_component& transform_comp
) -> void {
    mat4f local_matrix           = transform_comp.get_local_matrix();
    transform_comp.world_matrix_ = local_matrix;

    auto& reg = world_->registry();
    if (reg.has<hierarchy_component>(ent)) {
        const auto& hierarchy_comp = reg.get<hierarchy_component>(ent);
        if (hierarchy_comp.has_parent()) {
            entity parent = hierarchy_comp.get_parent();
            if (reg.has<transform_component>(parent)) {
                const auto& parent_transform_comp =
                    reg.get<transform_component>(parent);

                transform_comp.world_matrix_ =
                    parent_transform_comp.get_world_matrix() * local_matrix;
            }
        }
    }
}

template void transform_system::on_add<transform_component>(entity);
template void transform_system::on_add<spatial_component>(entity);

}  // namespace vw::ecs