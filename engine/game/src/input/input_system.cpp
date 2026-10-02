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
        input->incoming_     = frame;
        input->has_incoming_ = true;
    }
}

auto input_system::update(
    float32 delta_time
) -> void {
    submit(local_, mapper_.take_frame());

    world_->for_each<player_input_component>([delta_time](ecs::entity, player_input_component& input) {
        auto& frame = input.frame_;

        if (input.has_incoming_) {
            frame               = input.incoming_;
            input.has_incoming_ = false;
        } else {
            frame.pressed    = 0;
            frame.released   = 0;
            frame.zoom_delta = 0.0f;
        }

        for (std::size_t i = 0; i < input_action_count; ++i) {
            const auto action = static_cast<input_action>(i);
            auto& seconds     = input.hold_seconds_[i];

            if (frame.was_pressed(action)) {
                seconds = 0.0f;
            } else if (frame.is_held(action)) {
                seconds += delta_time;
            } else if (!frame.was_released(action)) {
                seconds = 0.0f;
            }
        }
    });
}

}  // namespace vw::game
