module vw.world;

import std;
import vw.core;

namespace vw::ecs {

structure_system::structure_system(world& w)
    : world_(&w) {}

structure_system::modifier::modifier(
    structure_component* component
)
    : component_(component) {}

auto structure_system::modifier::set_type(
    std::string_view type
) const -> void {
    component_->type_ = std::string{type};
}

auto structure_system::modifier::set_races(
    std::vector<std::string> races
) const -> void {
    component_->races_ = std::move(races);
}

auto structure_system::modifier::set_tier(
    uint8 tier
) const -> void {
    component_->tier_ = tier;
}

auto structure_system::modifier::set_size(
    structure_size size
) const -> void {
    component_->size_ = size;
}

auto structure_system::modify(
    entity ent
) -> modifier {
    return modifier(&world_->registry().get<structure_component>(ent));
}

auto structure_system::set_furniture_category(
    entity ent, std::string_view category
) -> void {
    world_->registry().get<furniture_point_component>(ent).category_ = std::string{category};
}

auto structure_system::set_connection_profile(
    entity ent, std::string_view profile
) -> void {
    world_->registry().get<connection_point_component>(ent).profile_ = std::string{profile};
}

}  // namespace vw::ecs
