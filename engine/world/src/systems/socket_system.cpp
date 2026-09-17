module vw.world;

import std;
import vw.core;

namespace vw::ecs {

socket_system::socket_system(world& w)
    : world_(&w) {}

auto socket_system::update(float32) -> void {}

auto socket_system::cleanup(
    entity ent
) -> void {
    auto& reg = world_->registry();
    if (!reg.has<socket_component>(ent)) {
        return;
    }
    auto& comp = reg.get<socket_component>(ent);
    for (auto& slot : comp.sockets_) {
        if (slot.attached.is_valid()) {
            world_->system<hierarchy_system>().modify(slot.attached).remove_parent();
            slot.attached = invalid_entity;
        }
    }
}

socket_system::socket_modifier::socket_modifier(
    socket_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto socket_system::modify(
    entity ent
) -> socket_modifier {
    return socket_modifier(this, ent);
}

auto socket_system::socket_modifier::attach(
    const std::string& socket_name, entity child
) -> socket_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<socket_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<socket_component>(entity_);
    auto it    = std::ranges::find_if(comp.sockets_, [&](const socket_point& sp) {
        return sp.name == socket_name;
    });
    if (it == comp.sockets_.end() || it->attached.is_valid()) {
        return *this;
    }
    it->attached = child;
    system_->world_->system<hierarchy_system>().modify(child).set_parent(entity_);
    system_->world_->system<transform_system>().modify(child)
        .set_position(it->position)
        .set_rotation(it->rotation)
        .set_scale(it->scale);
    return *this;
}

auto socket_system::socket_modifier::detach(
    const std::string& socket_name
) -> socket_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<socket_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<socket_component>(entity_);
    auto it    = std::ranges::find_if(comp.sockets_, [&](const socket_point& sp) {
        return sp.name == socket_name;
    });
    if (it == comp.sockets_.end() || !it->attached.is_valid()) {
        return *this;
    }
    auto detached = it->attached;
    it->attached  = invalid_entity;
    system_->world_->system<hierarchy_system>().modify(detached).remove_parent();
    return *this;
}

auto socket_system::socket_modifier::add_socket(
    const std::string& name, const vec3f& position, const quat& rotation, const vec3f& scale
) -> socket_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<socket_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<socket_component>(entity_);
    comp.sockets_.push_back(socket_point{name, position, rotation, scale, invalid_entity});
    return *this;
}

auto socket_system::socket_modifier::remove_socket(
    const std::string& name
) -> socket_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<socket_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<socket_component>(entity_);
    auto it    = std::ranges::find_if(comp.sockets_, [&](const socket_point& sp) -> auto {
        return sp.name == name;
    });
    if (it == comp.sockets_.end()) {
        return *this;
    }
    if (it->attached.is_valid()) {
        auto detached = it->attached;
        it->attached  = invalid_entity;
        system_->world_->system<hierarchy_system>().modify(detached).remove_parent();
    }
    comp.sockets_.erase(it);
    return *this;
}

}  // namespace vw::ecs
