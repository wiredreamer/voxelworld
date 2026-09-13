module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

animation_fsm_system::animation_fsm_system(world& w)
    : world_(&w) {}

auto animation_fsm_system::update(float32 /*dt*/) -> void {
    auto view =
        world_->registry()
            .view<animation_fsm_component, animation_player_component>();

    for (auto [ent, fsm_comp, player_comp] : view) {
        auto& triggers = fsm_comp.triggers_;
        for (std::size_t i = 0; i < fsm_comp.machine_count(); ++i) {
            auto pm = world_->system<animation_system>().modify_player(ent);
            if (!player_comp.has_layer(i)) {
                pm.add_layer(i);
                pm.rebuild_target_map();
            }

            auto& machine     = fsm_comp.machines_[i];
            const auto& layer = player_comp.get_layer(i);

            auto lm = pm.layer(i);

            if (layer.state == asset::animation_state::stopped && !layer.clip) {
                const auto* state = machine.get_current_state_node();
                if (!state) {
                    continue;
                }

                if (state->clip) {
                    lm.blend_to(state->clip);
                    lm.set_loop_mode(state->loop_mode);
                    lm.set_playback_speed(state->playback_speed);
                    lm.set_fade_out(state->layer_blend_out);

                    lm.play(state->layer_blend_in);
                    continue;
                }
            }

            auto result = machine.evaluate(layer, triggers);
            if (!result) {
                continue;
            }

            if (result->clip) {
                lm.blend_to(result->clip, result->blend);
                lm.set_loop_mode(result->loop_mode);
                lm.set_playback_speed(result->playback_speed);
                lm.set_fade_out(result->layer_blend_out);

                lm.play(result->layer_blend_in);
            }

            machine.apply_transition(*result);
        }
        triggers.clear();
    }
}

animation_fsm_system::modifier::modifier(
    animation_fsm_component* component
)
    : component_(component) {}

auto animation_fsm_system::modifier::add_machine(
    std::size_t index, asset::animation_fsm machine
) const -> void {
    if (index >= component_->machines_.size()) {
        component_->machines_.resize(index + 1);
    }
    component_->machines_[index] = std::move(machine);
}

auto animation_fsm_system::modifier::fire_trigger(
    std::string_view name
) const -> void {
    component_->triggers_.emplace(name);
}

auto animation_fsm_system::modify(
    entity ent
) -> modifier {
    auto& comp = world_->registry().get<animation_fsm_component>(ent);
    return modifier(&comp);
}

}  // namespace vw::ecs
