module vw.game;

import std;
import vw.core;
import vw.ecs;
import vw.world;

namespace vw::game {

input_system::input_system(
    ecs::world& w
)
    : world_{&w} {}

auto input_system::mapper() -> input_mapper& {
    return mapper_;
}

auto input_system::control_locally(
    ecs::entity ent
) -> void {
    local_ = ent;
    if (ent.is_valid() && !world_->has<player_input_component>(ent)) {
        world_->modify(ent).with<player_input_component>();
    }
}

auto input_system::locally_controlled() const -> ecs::entity {
    return local_;
}

auto input_system::submit(
    ecs::entity ent, const input_frame& frame
) -> void {
    if (auto* input = world_->try_get<player_input_component>(ent)) {
        input->frame_ = frame;
    }
}

auto input_system::update(
    float32
) -> void {
    const input_frame frame = mapper_.take_frame();
    submit(local_, frame);
}

}  // namespace vw::game
