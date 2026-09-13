module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

model_system::model_system(
    world& w
)
    : world_(&w) {}

template <typename C>
    requires std::same_as<C, model_component>
auto model_system::on_add(entity e) -> void {
    world_->registry().request_change<model_component>(e);
}

auto model_system::update(float32 /*dt*/) -> void {
    auto& reg       = world_->registry();
    auto& requested = reg.requested<model_component>();
    for (auto ent : requested) {
        reg.notify_changed<model_component>(ent);
    }
    reg.clear_requested<model_component>();
}

auto model_system::modify(
    entity e
) -> model_modifier {
    auto& comp = world_->registry().get<model_component>(e);
    return model_modifier(*this, &comp, e);
}

model_system::model_modifier::model_modifier(
    model_system& system, model_component* component, entity entity_id
)
    : system_(&system), component_(component), entity_(entity_id) {}

auto model_system::model_modifier::get_model() const -> std::shared_ptr<asset::model> {
    return component_->model_;
}

auto model_system::model_modifier::set_model(
    std::shared_ptr<asset::model> model_ptr
) -> void {
    component_->model_ = std::move(model_ptr);
    component_->chunk_.reset();
    system_->world_->registry().request_change<model_component>(entity_);
}

auto model_system::model_modifier::set_chunk(
    std::shared_ptr<asset::chunk_volume> volume
) -> void {
    component_->model_ = volume->shared_voxels();
    component_->chunk_ = std::move(volume);
    system_->world_->registry().request_change<model_component>(entity_);
}

auto model_system::model_modifier::set_voxel(
    int x, int y, int z, const voxel& v
) -> void {
    if (component_->model_) {
        component_->model_->set_voxel(x, y, z, v);
        system_->world_->registry().request_change<model_component>(entity_);
    }
}

auto model_system::model_modifier::set_voxel(
    vec3i pos, const voxel& v
) -> void {
    if (component_->model_) {
        component_->model_->set_voxel(pos.x, pos.y, pos.z, v);
        system_->world_->registry().request_change<model_component>(entity_);
    }
}

auto model_system::model_modifier::fill(
    const voxel& v
) -> void {
    if (component_->model_) {
        component_->model_->fill(v);
        system_->world_->registry().request_change<model_component>(entity_);
    }
}

template void model_system::on_add<model_component>(entity);

}  // namespace vw::ecs